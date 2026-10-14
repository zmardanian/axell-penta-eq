# Building Axell Penta EQ from source

The plugin is a CMake project. JUCE 8.0.15 is downloaded automatically at configure time (`FetchContent`), so you
only need a compiler, CMake and JUCE's Linux dependencies.

## 1. Dependencies

CMake ≥ 3.22, a C++17 compiler (releases are built with GCC 13), Ninja (optional, faster), git, and the X11 / ALSA /
FreeType / fontconfig development headers.

**Ubuntu / Debian:**

```bash
sudo apt install build-essential cmake ninja-build git pkg-config libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev
```

**Fedora:**

```bash
sudo dnf install gcc-c++ cmake ninja-build git pkgconf alsa-lib-devel freetype-devel fontconfig-devel libX11-devel libXcomposite-devel libXcursor-devel libXext-devel libXinerama-devel libXrandr-devel libXrender-devel mesa-libGL-devel
```

### Immutable distributions (Bazzite, Silverblue, Kinoite, …)

Don't layer build tools onto the host. Build inside a [distrobox](https://distrobox.it). Your home folder is shared
with the container, so the finished plugin is visible on the host:

```bash
bash scripts/setup-toolbox.sh   # creates the Ubuntu 24.04 box "juce-build" with the dependencies above
```

`scripts/build.sh`, `scripts/test.sh` and `scripts/setup-toolbox.sh` run inside that box. The plain CMake commands
below work the same anywhere.

## 2. Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

The bundle ends up in `build/AxellPentaEQ_artefacts/Release/VST3/Axell Penta EQ.vst3`.

| CMake option | Default | Effect |
|---|---|---|
| `PENTA_BUILD_TESTS` | `ON` | build `PentaDspTests` (plain C++17) and `PentaPluginTests` |
| `PENTA_BUILD_TOOLS` | `ON` | build `PentaGuiSnapshot` (editor → PNG) and `PentaCurvePlot` (preset curves → PNG) |
| `PENTA_SANITIZE` | `OFF` | build the test suites with AddressSanitizer + UBSan |

## 3. Test and validate

```bash
cmake --build build --target PentaDspTests PentaPluginTests -j"$(nproc)"
ctest --test-dir build --output-on-failure          # rewrites tests/report.md
```

With the helper scripts:

```bash
bash scripts/test.sh            # Release: full DSP suite + CPU benchmark, plugin suite
bash scripts/test.sh --asan     # Debug + ASan/UBSan/LSan (quick DSP grid), plugin suite
bash scripts/pluginval.sh       # pluginval strictness 10 (+ Steinberg validator if tools/vst3-validator exists)
```

The Steinberg validator is built from the official VST3 SDK (MIT) into `tools/`: clone
`https://github.com/steinbergmedia/vst3sdk` (with submodules) to `tools/vst3sdk`, move the GTK-based
`editorhost` example out of `public.sdk/samples/vst-hosting`, disable `audiohost/CMakeLists.txt`, then
`cmake --build build --target validator` and copy `build/bin/Release/validator` to `tools/vst3-validator`.
`scripts/vst3-validator-wrap.sh` adapts pluginval's module path to the SDK 3.8 validator.

Regenerate documentation images:

```bash
./build/PentaGuiSnapshot_artefacts/Release/PentaGuiSnapshot docs/screenshots
./build/PentaCurvePlot_artefacts/Release/PentaCurvePlot docs/preset-curves
```

## 4. Install your build

```bash
bash scripts/install.sh         # copies to ~/.vst3 and checks host libraries
bash scripts/check-reaper.sh    # REAPER's VST path includes ~/.vst3 and the plugin is in its scan cache
```

## Project layout

```
Source/Params.h            parameter table (ids, ranges, defaults) - single source of truth
Source/Presets.h           factory presets (overrides on the defaults)
Source/Engine.h            JUCE-free engine: segments, smoothing, phase modes, OS, mix, auto gain, analyzer taps
Source/dsp/Sections.h      analog prototypes, matched biquad design, TPT-SVF realisation
Source/dsp/Chain.h         section cascades with per-sample interpolation, parallel (character) form
Source/dsp/Bands.h         band types -> section cascades, detector/solo filters
Source/dsp/Rack.h          character modules (Inductor, Console E/G, Passive Tube, Air)
Source/dsp/Correction.h    minimum-phase / natural-phase correction FIRs
Source/dsp/LinearPhase.h   2x2 linear-phase FIR matrix, partitioned convolution, background designer
Source/dsp/Fft.h           radix-2 FFT
Source/dsp/Spectral.h      spectral dynamics: STFT detector, per-frequency law, minimum-phase FIR
Source/dsp/CurveFit.h      fit bands to a target curve (Draw, Match)
Source/Oversampling.h      linear-phase polyphase halfband oversampler (from Duo Vocal Comp)
Source/PluginProcessor.*   JUCE wrapper: parameters, state, presets, A/B, undo, buses
Source/PluginEditor.*, Source/gui/*   GUI (EqGraph: graph + workflow tools, MatchPanel, BandPanel, RackPanel, PresetBrowser)
tests/                     DSP suite, plugin suite, snapshot and curve-plot tools
docs/                      spec, research, decisions, screenshots, preset curves, logs
```

## Rules for contributors

- **Parameter IDs and order are frozen** once released (saved projects and automation). Add new parameters at the
  end of the table with `version = 2`; never rename, remove or reorder.
- `process()` must stay real-time safe: no allocation, locks, I/O or exceptions.
- `Source/Engine.h`, `Source/Params.h`, `Source/Presets.h` and `Source/dsp/*` stay free of JUCE.
- Latency must always be reported correctly (all phase modes, OS factors, after state restore).
- Keep both test suites, pluginval and the VST3 validator green.
- No trademarked product names in parameter names, choices, presets or tooltips (the plugin suite checks this).
