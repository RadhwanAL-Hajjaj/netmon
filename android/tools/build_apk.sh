#!/usr/bin/env bash
# Builds the netmon APK without Gradle: aapt2 for resources, kotlinc for code,
# dx for dex, zipalign and apksigner to finish. Used where Google's Maven
# repository is unreachable; Android Studio builds the same sources with Gradle.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/.." && pwd)"
# kotlinc 1.9.24, public android.jar files for API 34 and 35; see README.
TC="${TOOLCHAIN:-$PROJ/../tc}"
OUT="${OUT:-$PROJ/build-manual}"
APP="$PROJ/app/src/main"
PKG=com.example.netmon
VERSION_NAME=$(grep -oP 'VERSION_NAME = "\K[^"]+' "$APP/java/com/example/netmon/CrashLog.kt")
VERSION_CODE=$(grep -oP 'VERSION_CODE = \K[0-9]+' "$APP/java/com/example/netmon/CrashLog.kt")
COMPILE_JAR="$TC/android-35-public.jar"     # classes: what the code may call
RES_JAR="$TC/android-34-public.jar"         # framework resources this aapt2 can read
KOTLINC="$TC/k19/kotlinc/bin/kotlinc"
STDLIB="$TC/k19/kotlinc/lib/kotlin-stdlib.jar"
# Signing: your own keystore and its password, never committed. A keystore that
# does not exist yet is created at KEYSTORE with KS_PASS.
KEYSTORE="${KEYSTORE:-$PROJ/netmon-release.jks}"
KS_PASS="${KS_PASS:?set KS_PASS to your keystore password}"

rm -rf "$OUT"
mkdir -p "$OUT"/{gen,classes,stdlib}

echo "== resources"
# AGP takes the package from build.gradle's namespace; aapt2 wants it in the manifest.
sed "s|<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">|<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\" package=\"$PKG\">|" \
    "$APP/AndroidManifest.xml" > "$OUT/AndroidManifest.xml"
grep -q "package=\"$PKG\"" "$OUT/AndroidManifest.xml"
aapt2 compile --dir "$APP/res" -o "$OUT/res.zip"
aapt2 link -o "$OUT/base.apk" -I "$RES_JAR" --manifest "$OUT/AndroidManifest.xml" \
    --java "$OUT/gen" --min-sdk-version 26 --target-sdk-version 34 \
    --version-code "$VERSION_CODE" --version-name "$VERSION_NAME" "$OUT/res.zip"

echo "== R.java"
javac -nowarn -source 8 -target 8 -bootclasspath "$COMPILE_JAR" -d "$OUT/classes" \
    $(find "$OUT/gen" -name '*.java') 2>&1 | grep -v -E 'JAVA_TOOL_OPTIONS|warning: \[options\]|^[0-9]+ warning' || true

echo "== kotlin"
set +e
"$KOTLINC" -no-jdk -no-reflect -jvm-target 1.8 -Xlambdas=class -Xsam-conversions=class \
    -classpath "$COMPILE_JAR:$OUT/classes" -d "$OUT/classes" \
    $(find "$APP/java" -name '*.kt') > "$OUT/kotlinc.raw" 2>&1
rc=$?
set -e
grep -v JAVA_TOOL_OPTIONS "$OUT/kotlinc.raw" | tee "$OUT/kotlinc.log" || true
[ $rc -eq 0 ] || { echo "kotlinc failed"; exit 1; }

echo "== checks"
# ART has no LambdaMetafactory and dx cannot desugar, so no invokedynamic may
# reach the dex, and the few 1.9 stdlib functions that use it may not be called.
javap -c -p $(find "$OUT/classes" -name '*.class') > "$OUT/javap.txt" 2>/dev/null
if grep -n "invokedynamic" "$OUT/javap.txt"; then echo "invokedynamic in app classes"; exit 1; fi
if grep -nE "kotlin/comparisons/ComparisonsKt\.(compareBy:\(\[|then:|thenDescending:|nullsFirst:|nullsLast:)|kotlin/streams/jdk8/StreamsKt\.asStream" "$OUT/javap.txt"; then
    echo "call to an indy-backed stdlib function"; exit 1
fi
echo "no invokedynamic, no indy-backed stdlib calls"

echo "== dex"
(cd "$OUT/stdlib" && unzip -q -o "$STDLIB" && rm -rf META-INF)
ASM=/usr/share/java/asm.jar:/usr/share/java/asm-tree.jar
mkdir -p "$OUT/tools"
javac -nowarn -cp "$ASM" -d "$OUT/tools" "$HERE/StripIndy.java" 2>&1 | grep -v JAVA_TOOL_OPTIONS || true
java -cp "$ASM:$OUT/tools" StripIndy "$OUT/stdlib" 2>&1 | grep -v JAVA_TOOL_OPTIONS || true
dalvik-exchange --dex --min-sdk-version=26 --output="$OUT/classes.dex" "$OUT/classes" "$OUT/stdlib" 2>&1 | grep -v JAVA_TOOL_OPTIONS || true
test -s "$OUT/classes.dex"
if dexdump -d "$OUT/classes.dex" 2>/dev/null | grep -q "invoke-custom"; then echo "invoke-custom in dex"; exit 1; fi
echo "dex has no invoke-custom; $(dexdump -f "$OUT/classes.dex" 2>/dev/null | grep -m1 -E '^method_ids_size' | tr -s ' ')"

echo "== package"
cp "$OUT/base.apk" "$OUT/unsigned.apk"
(cd "$OUT" && zip -q -j unsigned.apk classes.dex)
zipalign -f -p 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"
if [ ! -f "$KEYSTORE" ]; then
    keytool -genkeypair -keystore "$KEYSTORE" -storetype PKCS12 -storepass "$KS_PASS" -keypass "$KS_PASS" \
        -alias netmon -keyalg RSA -keysize 2048 -validity 10950 \
        -dname "CN=netmon for Android, O=netmon" 2>&1 | grep -v JAVA_TOOL_OPTIONS || true
fi
apksigner sign --ks "$KEYSTORE" --ks-pass "pass:$KS_PASS" --key-pass "pass:$KS_PASS" --ks-key-alias netmon \
    --out "$OUT/netmon-$VERSION_NAME.apk" "$OUT/aligned.apk" 2>&1 | grep -v JAVA_TOOL_OPTIONS || true
apksigner verify --verbose "$OUT/netmon-$VERSION_NAME.apk" 2>&1 | grep -v JAVA_TOOL_OPTIONS | head -5
aapt2 dump badging "$OUT/netmon-$VERSION_NAME.apk" 2>/dev/null | head -4
ls -la "$OUT/netmon-$VERSION_NAME.apk"
