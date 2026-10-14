#!/usr/bin/env bash
# Installs Axell Penta EQ into ~/.vst3 (the per-user VST3 folder that Linux hosts scan).
# Only an older copy of Axell Penta EQ is ever replaced; nothing else in ~/.vst3 is touched.
set -euo pipefail
cd "$(dirname "$0")"

NAME="Axell Penta EQ.vst3"
DEST="$HOME/.vst3"
TARGET="$DEST/$NAME"

[ -d "$NAME" ] || { echo "Error: '$NAME' not found next to install.sh. Run it from the extracted release folder."; exit 1; }
[ "$(uname -m)" = "x86_64" ] || { echo "Error: this build is for x86_64 Linux, this machine is $(uname -m)."; exit 1; }

# an existing bundle counts as ours if its moduleinfo names it, or (older builds) it holds our binary
is_penta_bundle() {
    grep -q '"Name": "Axell Penta EQ"' "$1/Contents/Resources/moduleinfo.json" 2>/dev/null && return 0
    [ -f "$1/Contents/x86_64-linux/Axell Penta EQ.so" ]
}

mkdir -p "$DEST"
if [ -e "$TARGET" ] && ! is_penta_bundle "$TARGET"; then
    echo "Error: '$TARGET' exists but does not look like Axell Penta EQ. Not touching it."
    exit 1
fi

# copy to a temporary name first so an interrupted install never leaves a half-copied plugin
TMP=$(mktemp -d "$DEST/.axell-penta-eq-install.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
cp -r "$NAME" "$TMP/"
[ -e "$TARGET" ] && { echo "Replacing older copy: $TARGET"; rm -rf "$TARGET"; }
mv "$TMP/$NAME" "$TARGET"

SO="$TARGET/Contents/x86_64-linux/Axell Penta EQ.so"
if ldd "$SO" | grep -q "not found"; then
    echo "Installed to $TARGET, but these system libraries are missing:"
    ldd "$SO" | grep "not found"
    echo "Install them with your distribution's package manager (usually freetype and fontconfig)."
    exit 1
fi

echo "Installed: $TARGET"
echo "All required system libraries are present."
echo "Now restart your DAW so it scans the new plugin, then search for \"Penta\" in its FX browser."
