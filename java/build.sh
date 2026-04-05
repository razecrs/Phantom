#!/usr/bin/env bash
# java/build.sh — compile PhantomAgent into a DEX loadable by DexClassLoader
#
# Requirements:
#   1. Android build-tools in PATH (for d8):
#      export PATH="$PATH:/path/to/android-sdk/build-tools/<version>"
#   2. Rhino JAR at java/lib/rhino.jar
#      Download from: https://github.com/mozilla/rhino/releases (rhino-1.7.x.jar)
#   3. android.jar from SDK platforms dir:
#      java/lib/android.jar  (copy from android-sdk/platforms/android-34/android.jar)
#
# Output: java/out/phantom-agent.dex
#         Deploy to device: /data/phantom/lib/phantom-agent.dex
#                           /data/phantom/lib/rhino.jar

set -e

JAVA_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="$JAVA_DIR/out"
CP="$JAVA_DIR/lib/android.jar:$JAVA_DIR/lib/rhino.jar"

mkdir -p "$OUT/classes"

javac -source 8 -target 8 \
    -cp "$CP" \
    -d "$OUT/classes" \
    "$JAVA_DIR/src/dev/phantom/PhantomAgent.java"

# convert to DEX
d8 --output "$OUT" \
   --classpath "$JAVA_DIR/lib/android.jar" \
   --min-api 26 \
   "$OUT/classes/dev/phantom/PhantomAgent.class" \
   "$OUT/classes/dev/phantom/PhantomAgent\$JavaBridge.class" \
   "$OUT/classes/dev/phantom/PhantomAgent\$PhLog.class"

mv "$OUT/classes.dex" "$OUT/phantom-agent.dex"

echo "Built: $OUT/phantom-agent.dex"
echo ""
echo "Deploy with:"
echo "  adb push $OUT/phantom-agent.dex /data/phantom/lib/phantom-agent.dex"
echo "  adb push java/lib/rhino.jar     /data/phantom/lib/rhino.jar"
