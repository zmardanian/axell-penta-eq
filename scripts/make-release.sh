#!/usr/bin/env bash
# Builds the Linux release archive from the current Release build:
#   ~/axell-penta-eq-release/AxellPentaEQ-<ver>-linux-x86_64.tar.gz + SHA256SUMS.txt + release-notes.md
# Nothing is uploaded or pushed.
set -euo pipefail
cd "$(dirname "$0")/.."
VER=$(grep -oP 'project\(AxellPentaEQ VERSION \K[0-9.]+' CMakeLists.txt)
NAME="AxellPentaEQ-$VER-linux-x86_64"
OUT="${PENTA_RELEASE_DIR:-$HOME/axell-penta-eq-release}"
BUNDLE="build/AxellPentaEQ_artefacts/Release/VST3/Axell Penta EQ.vst3"
[ -d "$BUNDLE" ] || { echo "No Release build. Run scripts/build.sh first."; exit 1; }
rm -rf "$OUT/$NAME" "$OUT/$NAME.tar.gz"
mkdir -p "$OUT/$NAME"
cp -r "$BUNDLE" "$OUT/$NAME/"
strip --strip-unneeded "$OUT/$NAME/Axell Penta EQ.vst3/Contents/x86_64-linux/Axell Penta EQ.so" 2>/dev/null || true
cp packaging/install.sh packaging/uninstall.sh packaging/README.txt LICENSE THIRD_PARTY_NOTICES.md CHANGELOG.md "$OUT/$NAME/"
chmod +x "$OUT/$NAME/install.sh" "$OUT/$NAME/uninstall.sh"
(cd "$OUT" && tar --owner=0 --group=0 --sort=name -czf "$NAME.tar.gz" "$NAME" && sha256sum "$NAME.tar.gz" > SHA256SUMS.txt)
echo "Release: $OUT/$NAME.tar.gz"
cat "$OUT/SHA256SUMS.txt"

SHA=$(cut -d' ' -f1 "$OUT/SHA256SUMS.txt")
cat > "$OUT/release-notes.md" <<NOTES
First public release of **Axell Penta EQ**, an all-round EQ VST3 for Linux by Axell Yobag: 12 analog-matched parametric bands with dynamic EQ, Mid/Side and side-chain, plus a character rack (Inductor, Console E/G, Passive Tube, Air).

## Features

- 12 bands + HP/LP: bell, shelves, cuts (6–96 dB/oct with resonance), notch, band-pass, tilt
- Clean mode matches the analog curves within ±0.1 dB to 20 kHz (no cramping; the few documented exceptions near Nyquist are listed under *Accuracy* in the README); Character mode with proportional Q and saturating boosts
- Dynamic EQ on every gain band; detector from the band, another band's region or the external side-chain
- Stereo / L / R / Mid / Side per band, band solo, bypass
- Character rack before or after the EQ, global Drive, 1x/2x/4x oversampling
- Zero Latency, Natural Phase (2.7 ms) and Linear Phase (Low/Medium/High) with correct latency reporting
- Analyzer (pre / post / side-chain), static loudness-matched Auto Gain, Mix, In/Out gain
- 40 loudness-matched factory presets incl. 4 Axell Signature presets, user presets, preset browser with search, A/B, undo/redo
- Resizable GUI (75–200 %), HiDPI, tooltips on every control
- Passes pluginval at strictness 10 including the Steinberg VST3 validator, 624 DSP checks, 49 plugin checks and an independent black-box audit against textbook analog curves (scipy, separate VST3 host)

## Requirements

- x86_64 Linux, glibc 2.39+ (Bazzite, Fedora 40+, Ubuntu 24.04+)
- Any VST3 host. Tested in REAPER.

## Install

\`\`\`bash
tar xzf $NAME.tar.gz
cd $NAME
./install.sh
\`\`\`

Then restart your DAW and search for "Penta" in its FX browser. \`install.sh\` copies the plugin to \`~/.vst3\`, only ever replaces an older copy of Axell Penta EQ, and checks for missing system libraries. Run \`./uninstall.sh\` to remove it.

## SHA256

\`\`\`
$SHA  $NAME.tar.gz
\`\`\`

Verify with \`sha256sum -c SHA256SUMS.txt\`.

## Source and licence

Source code: https://github.com/zmardanian/axell-penta-eq (tag [\`v$VER\`](https://github.com/zmardanian/axell-penta-eq/tree/v$VER)). Build instructions: [docs/DEVELOPMENT.md](https://github.com/zmardanian/axell-penta-eq/blob/v$VER/docs/DEVELOPMENT.md).

Licensed under the GNU AGPLv3 (built with JUCE 8.0.15 under its AGPLv3 option). Third-party notices are in \`THIRD_PARTY_NOTICES.md\`.
NOTES
echo "Release notes: $OUT/release-notes.md"
