#!/usr/bin/env bash
# Draws the radars, the Finder, the signal trace, the heat bar and the network
# map with the app's own painters (app/src/main/java/.../ui/Painters.kt) and
# writes them as PNG files, so changes to the drawing can be looked at without
# a phone. android.graphics is stood in for by shots/FakeGraphics.kt, which
# draws with java.awt; text metrics differ a little from a phone's.
#
#   TOOLCHAIN=/path/to/tc tools/test/shots.sh [output folder, default build-manual/shots]
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PROJ="$(cd "$HERE/../.." && pwd)"
TC="${TOOLCHAIN:-$PROJ/../tc}"
JSON="${JSON_JAR:-/usr/share/java/com.android.json.jar}"
SRC="$PROJ/app/src/main/java/com/example/netmon"
SHOTS="$(mkdir -p "${1:-$PROJ/build-manual/shots}" && cd "${1:-$PROJ/build-manual/shots}" && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# The simulated LAN of the firmware's own page tests.
python3 - "$PROJ/../firmware/test/pages" > "$OUT/lan.json" <<'PY'
import json, sys
sys.path.insert(0, sys.argv[1])
sys.argv = sys.argv[:1]
import mock_nearby
print(json.dumps({"map": mock_nearby.mapdata(), "devices": mock_nearby.devices()}))
PY

JAVA_OPTS="${JAVA_OPTS:--Xmx1g}" "$TC/k19/kotlinc/bin/kotlinc" -jvm-target 17 -nowarn -classpath "$JSON" -d "$OUT/classes" \
    "$HERE/shots/FakeGraphics.kt" "$HERE/shots/RenderShots.kt" "$SRC/ui/Painters.kt" "$SRC/ui/Theme.kt" \
    "$SRC/Models.kt" "$SRC/Parse.kt" "$SRC/NetmonClient.kt" "$SRC/Format.kt" "$SRC/Nearby.kt" "$SRC/Finder.kt" \
    "$SRC/NetMap.kt" "$SRC/EventHistory.kt" "$SRC/Subnet.kt" 2>&1 | grep -v JAVA_TOOL_OPTIONS || true

LAN="$OUT/lan.json" SHOTS="$SHOTS" java -cp "$OUT/classes:$JSON:$TC/k19/kotlinc/lib/kotlin-stdlib.jar" RenderShotsKt 2>&1 \
    | grep -v JAVA_TOOL_OPTIONS
