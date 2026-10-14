#!/usr/bin/env bash
# Removes Axell Penta EQ from ~/.vst3. Your own presets are kept.
set -euo pipefail

TARGET="$HOME/.vst3/Axell Penta EQ.vst3"
PRESETS="${XDG_DATA_HOME:-$HOME/.local/share}/AxellPentaEQ/Presets"

if [ ! -e "$TARGET" ]; then
    echo "Axell Penta EQ is not installed in ~/.vst3. Nothing to do."
    exit 0
fi
if ! grep -q '"Name": "Axell Penta EQ"' "$TARGET/Contents/Resources/moduleinfo.json" 2>/dev/null \
   && [ ! -f "$TARGET/Contents/x86_64-linux/Axell Penta EQ.so" ]; then
    echo "Error: '$TARGET' does not look like Axell Penta EQ. Not touching it."
    exit 1
fi

rm -rf "$TARGET"
echo "Removed: $TARGET"
echo "Restart your DAW so it forgets the plugin."
if [ -d "$PRESETS" ]; then
    echo "Your presets were kept in: $PRESETS"
    echo "Delete that folder too if you don't need them any more."
fi
