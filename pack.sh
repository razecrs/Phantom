#!/usr/bin/env bash
# pack.sh — assemble flashable Phantom zip from build artifacts
# Usage: bash pack.sh
# Output: build/phantom-module.zip

set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"
STAGE="$BUILD/stage"
OUT="$BUILD/phantom-module.zip"

rm -rf "$STAGE"
mkdir -p "$STAGE/zygisk"
mkdir -p "$STAGE/bin"
mkdir -p "$STAGE/scripts"

# ── Zygisk SO files ──────────────────────────────────────────────
cp "$ROOT/module/target/aarch64-linux-android/release/libphantom.so" \
   "$STAGE/zygisk/arm64-v8a.so"
cp "$ROOT/module/target/armv7-linux-androideabi/release/libphantom.so" \
   "$STAGE/zygisk/armeabi-v7a.so"

# ── Shell + tools ───────────────────────────────────────────────
cp "$BUILD/psh_arm64"               "$STAGE/bin/psh_arm64"
cp "$BUILD/psh_arm32"               "$STAGE/bin/psh_arm32"
cp "$BUILD/phantom-daemon_arm64"    "$STAGE/bin/phantom-daemon_arm64"
cp "$BUILD/phantom-hub_arm64"       "$STAGE/bin/phantom-hub_arm64"
cp "$BUILD/phantom-lsp_arm64"       "$STAGE/bin/phantom-lsp_arm64"
cp "$BUILD/phantom-bootstrap_arm64" "$STAGE/bin/phantom-bootstrap_arm64"
cp "$BUILD/phantom-bundler_arm64"   "$STAGE/bin/phantom-bundler_arm64"
cp "$BUILD/phantom-updater_arm64"   "$STAGE/bin/phantom-updater_arm64"

# ── Default scripts ──────────────────────────────────────────────
cp -r "$ROOT/scripts/"* "$STAGE/scripts/"

# ── Module metadata ──────────────────────────────────────────────
cp "$ROOT/dist/module.prop"          "$STAGE/module.prop"
cp "$ROOT/dist/customize.sh"         "$STAGE/customize.sh"
cp "$ROOT/dist/service.sh"           "$STAGE/service.sh"
cp "$ROOT/shell/prompt.toml.example" "$STAGE/prompt.toml.example"

# ── Zip it ───────────────────────────────────────────────────────
rm -f "$OUT"
if command -v zip >/dev/null 2>&1; then
    cd "$STAGE" && zip -r9 "$OUT" .
else
    # Windows: PowerShell with forward-slash entry names (required for Android unzip)
    WIN_STAGE="$(cygpath -w "$STAGE")"
    WIN_OUT="$(cygpath -w "$OUT")"
    powershell.exe -NoProfile -Command "
        Add-Type -Assembly System.IO.Compression.FileSystem
        Add-Type -Assembly System.IO.Compression
        \$stage = '$WIN_STAGE'
        \$zip = [System.IO.Compression.ZipFile]::Open('$WIN_OUT', 'Create')
        Get-ChildItem -Path \$stage -Recurse -File | ForEach-Object {
            \$entryName = \$_.FullName.Substring(\$stage.Length).TrimStart('\\').Replace('\\', '/')
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(\$zip, \$_.FullName, \$entryName, 'Optimal') | Out-Null
        }
        \$zip.Dispose()
    "
fi

echo "Packed: $OUT"
ls -lh "$OUT"
