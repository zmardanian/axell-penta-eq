# Workflow study for 2.0 (internal)

Request (2026-10-14): study which characteristics of the leading modern digital EQ (the FabFilter Pro-Q
line was named as the reference) could be brought to Axell Penta EQ — specifically its **spectral dynamics**
and its **extensive workflow tools** — then implement them for 2.0.

Clean-room basis (see `clean-room.md`): only publicly described *behaviour* (product manuals, public videos
and reviews describing what a feature does for the user) and DSP theory were used. No code, graphics, data or
UI layout were taken. Every feature below is our own design and implementation, with generic names in the UI
("Spectral", "Draw", "Match", "Gain Scale"); the plugin's clean-room scan (PentaPluginTests) still covers every
user-visible string.

## 1. Spectral dynamics

**What the reference does (publicly described):** a dynamic band can work "spectrally": instead of the whole
band moving, each frequency inside the band reacts on its own, so only resonances or harsh components that
exceed the threshold are reduced (or expanded), with the rest of the band untouched.

**Our design (Source/dsp/Spectral.h, DECISIONS D29):**

| Aspect | Choice | Why |
|---|---|---|
| Detector | STFT of the band's detector signal (input or side-chain): Hann, N = 1024 at 44.1/48 kHz (scaled with the rate), hop N/4 | 47 Hz bins, 5.3 ms hop: resolves resonances in the mids/highs where spectral control matters |
| Level per frequency | calibrated so a steady tone of amplitude A reads 20 log10 A dBFS; the loudest within ±2 bins | a tone's energy spreads over the Hann main lobe; without the ±2-bin view its own frequency would not get the full range |
| Law | the band's own threshold / ratio / range / attack / release, 6 dB soft knee, per frequency | same controls as broadband, so switching mode is one click |
| Where it acts | the band's own shape at +12 dB, normalised to 0..1, weights the change per frequency | the band decides the region (a bell = its bump, a shelf = above / below the corner) |
| Realisation | the gain curve → minimum-phase FIR (real cepstrum), N/2 taps per base-rate factor, applied right after the band's filter in its own L/R/M/S domain, crossfaded per hop | **zero added latency** in every phase mode; an STFT resynthesis would add a frame of latency |
| Determinism | hops tied to the absolute sample position | identical output for any host block size (DSP suite C) |
| Idle cost | a band below threshold everywhere bypasses the convolution (history only) | no CPU when nothing rings |

Measured: a loud tone in the band is reduced by the range (−11.8…−11.9 of −12 dB), a quiet tone 0.8 kHz away
inside the same band by 0.000 dB (broadband mode: −8.3 dB); no added latency; identical in Zero Latency,
Natural Phase, Linear Phase and with 2x/4x oversampling; independent audit through a separate VST3 host.
Cost ≈ 0.6 % of one core per acting spectral band at 48 kHz.

**Calibration note for users:** per-frequency levels are lower than broadband levels for noise-like material
(broadband noise at −18 dBFS reads ≈ −40 dBFS per frequency with N = 1024). Loud music reads about −45 to −25
dBFS per frequency, so thresholds around −30 dB catch ringing peaks while the rest passes. The tooltip says so.

## 2. Workflow tools

| Characteristic of the reference (public behaviour) | 2.0 implementation | Notes |
|---|---|---|
| Select several bands, move them together | Ctrl/Cmd+click toggles; a frame drag on empty space selects; dragging a selected node moves all of them (same frequency ratio, same dB change) | Frame drag needs no modifier: a plain click still adds a band (the drag distance decides) |
| Keyboard editing | arrows: ±1 semitone / ±0.5 dB (Shift: 1/96 octave / 0.1 dB); Del; Ctrl+A; Esc; Tab / Shift+Tab; I (invert); B (bypass); Ctrl+C / Ctrl+V | A burst of key repeats is one undo step |
| Gain scaling of the whole EQ | **Gain Scale** 0–200 % (automatable, appended parameter, version hint 2) | Applies in the audio path, the display, auto gain and Linear Phase alike (CurveModel) |
| Invert band gains | I key / menu, on the selection | |
| Copy / paste bands | Ctrl+C / Ctrl+V, menus; the clipboard is shared by every instance in the host process | Paste onto the selection, or into a free band |
| Note display, pitch snapping | the readout shows the note (e.g. "A4 +20ct"); "Snap frequency to notes" (A4 = 440 Hz) | Option saved with the UI state |
| Grab a band from a spectrum peak | Alt+click on empty space: a narrow bell on the nearest peak (±1/3 oct), refined by parabolic interpolation on the analyzer's FFT bins; Alt shows the peak first | Exact on a test tone (3000 Hz) |
| Draw / sketch an EQ curve | **DRAW**: paint the target, APPLY fits up to 12 bands | `CurveFitter` (Source/dsp/CurveFit.h): greedy bell/shelf placement on the largest deviation, then joint damped Gauss-Newton on the analog band curves. Recovers known bands exactly; freehand curves within ≈ 0.04 dB RMS |
| Match EQ to a reference | **MATCH**: capture a reference (side-chain, input, or a reference captured in another instance of the session) and this track; level-normalised difference with amount / smoothing / max; fitted to bands | Same fitter; the preview shows both spectra and the correction |
| Freeze the analyzer | Analyzer menu / graph menu | |
| Undo / redo, A/B, auto gain, M/S per band, solo, zero / natural / linear phase, dynamic EQ with side-chain | already in 1.0 (A/B saved with the session since 2.0) | |

## 3. Studied and deliberately not done (yet)

| Characteristic | Why not in 2.0 |
|---|---|
| Instance list / editing other instances' bands, collision detection | Needs a process-wide registry of live instances and thread-safe cross-instance parameter access; the clipboard and the shared Match reference cover the common cases. Candidate for a later release |
| MIDI learn | Not part of the VST3 parameter model on Linux hosts in a portable way; host automation / controller mapping covers it |
| More than 12 bands | 12 + dedicated HP/LP covers every preset and the measured use; more bands would grow the parameter list (automation lanes) by 17 per band |
| Spectral resynthesis (STFT overlap-add) for spectral dynamics | Adds a frame of latency; the minimum-phase FIR keeps Zero Latency |
| Per-instance spectrum overlay of another track ("show other instance's spectrum") | Part of the instance-list idea above |

## 4. Verification

- DSP suite (tests/DspTests.cpp): spectral selectivity, upward mode, side-chain, M/S, below-threshold identity,
  block-size invariance with a spectral band, click-free mode switching, zero allocations, CPU benchmark.
- Plugin suite (tests/PluginTests.cpp, section 9): old sessions load the new parameters at their defaults,
  version hints, Gain Scale on the audio, frame selection, group drag, keyboard, invert, copy/paste, delete +
  undo, note readout and snapping, spectrum-peak grab, Draw → APPLY, Match → APPLY, band panel controls.
- Independent audit (tests/independent): spectral selectivity and latency in every phase mode and Gain Scale
  through a separate VST3 host.
