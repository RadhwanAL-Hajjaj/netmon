#!/usr/bin/env bash
# Runs CoreTest.kt: the app's plain-Kotlin parts (parsers, wording, the Nearby,
# Finder and map logic, the HTTP client) on the JVM, against the mock board,
# and, where node is installed, against the numbers the board's own web pages
# compute (web_parity.py). Then BeeperTest.kt, the Finder's beeps against a
# stand-in AudioTrack.
#
#   TOOLCHAIN=/path/to/tc tools/test/run.sh
#
# Needs kotlinc 1.9 (TOOLCHAIN/k19, as for build_apk.sh) and Android's org.json
# for the JVM (Debian and Ubuntu: libandroid-json-java).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/../.." && pwd)"
TC="${TOOLCHAIN:-$PROJ/../tc}"
KOTLINC="$TC/k19/kotlinc/bin/kotlinc"
STDLIB="$TC/k19/kotlinc/lib/kotlin-stdlib.jar"
JSON="${JSON_JAR:-/usr/share/java/com.android.json.jar}"
SRC="$PROJ/app/src/main/java/com/example/netmon"
PAGES="$PROJ/../firmware/netmon/src/hw/pages.h"
PORT="${PORT:-18080}"
OUT="$(mktemp -d)"
trap 'kill "$MOCK_PID" 2>/dev/null || true; rm -rf "$OUT"' EXIT

python3 "$PROJ/tools/mock_board.py" "$PORT" > "$OUT/mock.log" 2>&1 &
MOCK_PID=$!

# Stand-ins for the firmware images the upload checks send: the ESP32 magic
# byte, the netmon version string as a build carries it, and some bulk.
mkdir -p "$OUT/bins"
python3 - "$OUT/bins" <<'PY'
import os, sys
for f, v in (("netmon-0.9.4.ino.bin", "0.9.4-history-upload"), ("netmon-0.9.5.ino.bin", "0.9.5-dhcp-names"),
             ("netmon-0.9.6.ino.bin", "0.9.6-status-hints")):
    body = bytearray(os.urandom(400_000))
    body[0] = 0xE9
    tag = b"\0" + v.encode() + b"\0"
    body[123_456:123_456 + len(tag)] = tag
    open(os.path.join(sys.argv[1], f), "wb").write(bytes(body))
PY

if command -v node > /dev/null && [ -f "$PAGES" ]; then
    python3 "$HERE/web_parity.py" "$PAGES" "$OUT/parity.json"
else
    echo "node or pages.h missing: skipping the comparison with the web pages"
fi

JAVA_OPTS="${JAVA_OPTS:--Xmx1g}" "$KOTLINC" -jvm-target 17 -nowarn -classpath "$JSON" -d "$OUT/classes" \
    "$SRC/Models.kt" "$SRC/Parse.kt" "$SRC/NetmonClient.kt" "$SRC/Format.kt" "$SRC/Firmware.kt" \
    "$SRC/Validate.kt" "$SRC/Subnet.kt" "$SRC/EventHistory.kt" "$SRC/AlertRules.kt" "$SRC/LatencyLog.kt" \
    "$SRC/Nearby.kt" "$SRC/Finder.kt" "$SRC/NetMap.kt" "$SRC/LinkCodec.kt" "$SRC/RoutePlan.kt" "$SRC/Reports.kt" \
    "$HERE/CoreTest.kt" 2>&1 | grep -v JAVA_TOOL_OPTIONS || true

BINS="$OUT/bins" MOCK="http://127.0.0.1:$PORT" PARITY="$OUT/parity.json" \
    java -cp "$OUT/classes:$JSON:$STDLIB" CoreTestKt 2>&1 | grep -v JAVA_TOOL_OPTIONS

# The Finder's beeps, with android.media stood in for (tools/test/beeper).
JAVA_OPTS="${JAVA_OPTS:--Xmx1g}" "$KOTLINC" -jvm-target 17 -nowarn -d "$OUT/beeper" \
    "$SRC/Beeper.kt" "$HERE"/beeper/*.kt 2>&1 | grep -v JAVA_TOOL_OPTIONS || true
java -cp "$OUT/beeper:$STDLIB" BeeperTestKt 2>&1 | grep -v JAVA_TOOL_OPTIONS
