# Changelog

## 2.0.0 - 2026-10-14

New:

- **Spectral dynamics.** A dynamic band can work per frequency (Dynamic Mode = Spectral): only what is too loud
  inside the band is reduced or raised - a resonance, a whistle, a harsh partial, a sharp sibilant - while the rest
  of the band stays untouched. Zero added latency in every phase mode; the graph shows the live per-frequency gain.
- **Workflow tools** in the graph:
  - select several bands (Ctrl/Cmd+click, or drag a frame on empty space) and move them together;
    Ctrl/Cmd+drag = Q
  - keyboard: arrows nudge frequency (1 semitone) / gain (0.5 dB), Shift = fine, Del removes, Ctrl+A,
    Esc, Tab, I inverts gains, B bypasses, Ctrl+C / Ctrl+V copy and paste a band (also between instances)
  - note readout (e.g. "A4 +20ct") and optional snapping to semitones
  - Alt+click on empty space puts a band on the nearest spectrum peak
  - analyzer freeze, a graph menu (right-click on empty space)
- **Draw**: paint the curve you want, APPLY fits up to 12 bands to it.
- **Match**: capture a reference (side-chain, this input, or another instance of the EQ in the session) and
  your track; the EQ moves your track's tonal balance towards the reference (amount, smoothing, max correction).
- **Gain Scale** (0-200 %): scales the gain of every band.
- Two new presets: *Vocal - Spectral Harshness Tamer*, *Mix Bus - Spectral Resonance Control* (42 in total).

Better:

- **No accuracy exceptions any more**: Clean mode now follows the analog curve within ±0.1 dB to 20 kHz for every
  band type, gain, Q and frequency, at 44.1/48 kHz without oversampling too. Sections near Nyquist are realised as
  fitted 4th-order pairs; resonant shelves and tilt with Q above 5, notches and band-passes above 18 kHz and band
  frequencies up to 30 kHz used to miss by up to ~15 dB there. Notch and band-pass now act at their true centre
  above Nyquist too (they were held at 0.49 × the sample rate).
- Natural Phase: steep, narrow band-passes (72/96 dB/oct, Q above 6) are now exact as well (were up to 0.3 dB off);
  Natural Phase follows the analog phase more closely in general (audit: worst 2.1° → 0.7°).
- Automating near-Nyquist bands is cleaner than before (a resonant high shelf sweep at 44.1 kHz: −92 dBFS of
  high-frequency residue, was −42 dBFS).

Fixed:

- A/B: both slots and the active slot are saved with the session. A reopened project used to keep only the slot
  that was playing, and always opened on A.
- Phase mode / oversampling changes during playback: the output stays muted until the host has been told the new
  latency, so it is never heard with the old delay compensation (previously up to ~50 ms could play misaligned,
  e.g. after Linear Phase → Zero Latency). The latency is also handed between threads safely now.

Compatibility: sessions and presets from 1.0 open unchanged (the new parameters are appended and start at
Broadband / 100 %; existing parameter IDs and automation are untouched). Typical sessions use the same CPU as 1.0.

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
