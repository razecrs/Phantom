#!/system/bin/sh
# Phantom install script — runs inside KSU / Magisk / ReZygisk installer
#
# Requirements:
#   Root manager : KernelSU, Magisk, or APatch
#   Zygisk       : ReZygisk (PerformanC) recommended — supports Android 12-16.
#                  ZygiskNext is archived; do NOT use it on Android 14+.
#   Android 13+  : MEETS_DEVICE_INTEGRITY requires Tricky Store if app checks it.

PHANTOM_DIR=/data/phantom

ui_print "- Installing Phantom v$(grep '^version=' "$MODPATH/module.prop" | cut -d= -f2)..."

# install psh shell
mkdir -p "$PHANTOM_DIR/bin"
if [ "$ARCH" = "arm64" ]; then
    cp "$MODPATH/bin/psh_arm64"        "$PHANTOM_DIR/bin/psh"
    cp "$MODPATH/bin/phantom-daemon_arm64"  "$PHANTOM_DIR/bin/phantom-daemon"
    cp "$MODPATH/bin/phantom-hub_arm64"     "$PHANTOM_DIR/bin/ph-hub"
    cp "$MODPATH/bin/phantom-bootstrap_arm64" "$PHANTOM_DIR/bin/ph-bootstrap"
    cp "$MODPATH/bin/phantom-lsp_arm64"     "$PHANTOM_DIR/bin/ph-lsp"
    cp "$MODPATH/bin/phantom-bundler_arm64" "$PHANTOM_DIR/bin/ph-bundler"
    cp "$MODPATH/bin/phantom-updater_arm64" "$PHANTOM_DIR/bin/ph-updater"
else
    ui_print "! ARM32 device — installing 32-bit shell (no Go tools)"
    cp "$MODPATH/bin/psh_arm32" "$PHANTOM_DIR/bin/psh"
fi
chmod 755 "$PHANTOM_DIR/bin/"*

# install default scripts
mkdir -p "$PHANTOM_DIR/scripts"
cp -r "$MODPATH/scripts/"* "$PHANTOM_DIR/scripts/" 2>/dev/null || true

# drop prompt config example
[ -f "$PHANTOM_DIR/prompt.toml" ] || cp "$MODPATH/prompt.toml.example" "$PHANTOM_DIR/prompt.toml"

# install service script (auto-starts daemon at boot)
cp "$MODPATH/service.sh" "$MODPATH/service.sh"   # already in MODPATH, magisk picks it up

# create runtime dirs
mkdir -p /dev/phantom
chmod 777 /dev/phantom
mkdir -p "$PHANTOM_DIR"
chmod 755 "$PHANTOM_DIR"

ui_print "- Phantom installed to $PHANTOM_DIR"
ui_print "- Traffic daemon starts automatically at boot on port 7777"
ui_print "- Live view: $PHANTOM_DIR/bin/ph-hub"
ui_print "- Run shell: $PHANTOM_DIR/bin/psh"
