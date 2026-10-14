# Independent fidelity audit

The DSP suite (`tests/DspTests.cpp`) compares the engine with its own analog model (`CurveModel`). That catches
realisation errors, but not a mistake that the engine and its model share. This audit closes that gap. It is a
black-box test of the **built plugin bundle** through a real VST3 host. It shares no code with the plugin:

- **`AuditHost.cpp`** → `PentaAuditHost`. A small host built on JUCE's VST3 *hosting* code; none of the plugin's
  classes are compiled in. It loads the `.vst3` and sets parameters **by display name and text** through the
  plugin's own string parser, as a host would. It renders raw float32 files with any host block size (fixed or
  random), mid-render automation and a side-chain.
- **`lib.py`**: the analog references, written from the textbook forms and evaluated with **scipy**. Butterworth
  comes from `scipy.signal.buttap`'s own pole placement. Bells and shelves are the RBJ cookbook prototypes. The
  response is exact DFT bins of long impulse responses.
- **`audit_mag.py`** (part 1): random settings of every band type at 44.1–192 kHz, in Zero Latency and Natural
  Phase. It covers the full Q range of shelves and tilt, and band frequencies up to 30 kHz. Pass criterion: the
  spec (≤ 0.1 dB where the curve is ≥ −24 dB; cuts ≤ 0.5 dB down to −48 dB; Natural Phase also matches the analog
  phase). Since 2.0 no setting is exempt: resonant shelves and tilt up to Q 24, notch and band-pass at their true
  centre (also above Nyquist) and every type at band frequencies of 12–30 kHz are checked at 44.1/48 kHz without
  oversampling, in Zero Latency and Natural Phase. (The *known limits* hook stays for future features; it lists
  settings outside the documented envelope separately, with their worst error, never dropping them.)
- **`audit_misc.py`** (part 2), the processing checks:
  - transparency of default settings, all modes, OS factors and rates
  - reported latency = actual latency
  - Clean bands linear and time-invariant at Drive 100 and +3.5 dBFS input
  - bit-identical output for random host block sizes
  - Stereo/L/R/M/S algebra
  - oversampling passband
  - latency-aligned Mix
  - exact gains
  - Linear Phase symmetry and accuracy
  - no clicks for frequency/gain sweeps, type, phase-mode and oversampling switches during playback
  - live latency changes: with a message loop between blocks (`pdc` job option), the host logs the latency it
    compensates per block; through repeated phase-mode switches the output must be the input delayed by exactly
    that latency, times a fade gain in [0, 1], at every sample
  - dynamic-band static curve and side-chain
  - spectral dynamics (2.0): a loud tone in the band reduced by the range while a quiet tone in the same band is
    untouched, in every phase mode, with no added latency; broadband mode for comparison
  - Gain Scale (2.0): a +8 dB bell at 50 % is exactly +4 dB in every phase mode
  - decay to silence

```bash
bash scripts/audit.sh                      # audits build/…/Axell Penta EQ.vst3
bash scripts/audit.sh ~/.vst3/"Axell Penta EQ.vst3"   # or any installed bundle
```

The host is built inside the distrobox. The Python part runs wherever `python3` with numpy and scipy exists
(the Bazzite host has both). The report goes to `audit-report.txt` and the exit code is the number of failures.

## What it found (2026-10-14, before v1.0.0 was published)

| Finding | Effect | Fix |
|---|---|---|
| Butterworth Q for odd orders used the even-order formula | 18 dB/oct cuts and the 18 dB/oct HPFs of Inductor and Console were −7.8 dB at the corner instead of −3 dB, and still about −2 dB at twice the frequency | correct pole angles (D18) |
| 3-point numerator fit on resonant shelves/tilt | shelves and tilt with Q ≳ 2 missed the analog curve by up to 6.6 dB (Q 2–5), and by up to 27 dB at extreme Q | per section, the closer of fit and matched-Z (D19) |
| Global warm-up after a phase-mode switch was shorter than the Linear Phase latency | a click about 0.1 s after switching *to* Linear Phase during playback | warm-up covers the new latency (D20) |
| Float one-pole gain smoothing stalled short of its target | gains and Mix stayed up to ~0.0005 dB off | snap when a step stops moving (D21) |
| Band-pass 72/96 dB/oct were the same filter as 48 dB/oct | the slope menu overstated the steepness | n = slope/12 sections, up to 8 |
| A band switched to a cut kept Q 1.0 | new cut bands had a +1.3 to +2.9 dB bump at the corner | a type change by the user resets a neutral Q (D22) |

After v1.0.0, an external code review reported two more issues, both confirmed and fixed:

| Finding | Effect | Fix |
|---|---|---|
| A/B slots were not part of the session state | a reopened project kept only the slot that was playing | the state stores the A/B pair (D23) |
| Latency handed from the audio thread to the host timer through a plain `int`; the engine faded in without waiting for the host | a data race; after a latency-changing switch, up to ~50 ms played with the old compensation (this audit's live check: ~3000 misaligned windows with v1.0.0, 0 now) | atomic handshake: a new latency plays only after it was reported; without a report the old mode stays (D24, completed in 2.0.1) |
