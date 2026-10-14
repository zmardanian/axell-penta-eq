#!/usr/bin/env bash
# Downloads pluginval (once) and validates the built plugin at strictness 10.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tools
if [ ! -x tools/pluginval ]; then
  curl -fL -o tools/pluginval.zip https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Linux.zip
  (cd tools && unzip -o pluginval.zip && chmod +x pluginval)
fi
BUNDLE="build/AxellPentaEQ_artefacts/Release/VST3/Axell Penta EQ.vst3"
VAL=()
[ -x tools/vst3-validator ] && VAL=(--vst3validator "$PWD/scripts/vst3-validator-wrap.sh")
./tools/pluginval --strictness-level 10 --validate-in-process "${VAL[@]}" --output-dir tools/pluginval-logs --validate "$BUNDLE" "$@"
