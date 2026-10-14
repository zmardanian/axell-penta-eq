# Changelog

## 1.0.0 - 2026-10-14

First release.

- 12 parametric bands + dedicated HP/LP: bell, low/high shelf, low/high cut (6–96 dB/oct, resonance), notch,
  band-pass, tilt. Clean (analog-matched, ±0.1 dB to 20 kHz) and Character (proportional Q, saturating boosts) modes.
- Dynamic EQ on every gain band: threshold, range (cut or boost), ratio, attack, release; detector from the band,
  another band's region or the external side-chain.
- Stereo / Left / Right / Mid / Side placement per band; band solo; bypass.
- Character rack: Inductor, Console E/G, Passive Tube, Air - before or after the EQ, with global Drive and
  1x/2x/4x oversampling.
- Phase modes: Zero Latency, Natural Phase (2.7 ms), Linear Phase (Low/Medium/High); latency always reported.
- Input/Output gain, Mix, static loudness-matched Auto Gain.
- Analyzer: pre, post and side-chain spectra, smoothing, tilt, peak hold.
- 40 loudness-matched factory presets in five categories incl. four Axell Signature presets; user presets with
  descriptions; preset browser with search; A/B; undo/redo.
- Resizable GUI 75–200 %, HiDPI, tooltips on every control.
- Verified: DSP suite (all checks pass at 44.1–192 kHz), plugin suite, pluginval strictness 10 with the Steinberg
  VST3 validator, offline render in REAPER, and an independent black-box audit through a separate VST3 host against
  textbook curves computed with scipy (`tests/independent`).

Fixed before release (found by the independent audit, see `tests/independent/README.md`):
- 18 dB/oct cuts and the Inductor/Console 18 dB/oct high-pass filters are now true Butterworth (−3 dB at the set
  frequency; they were −7.8 dB). The seven presets that use them keep slightly more low-mids and were re-trimmed.
- Resonant shelves and tilt (Q above ~2) now follow the analog curve; they were off by several dB.
- No click when switching to Linear Phase during playback.
- Gains and Mix land exactly on their value.
- Band-pass 72 and 96 dB/oct are now steeper than 48 dB/oct.
- A band switched to Low/High Cut starts at Q 0.71 (no corner bump).
