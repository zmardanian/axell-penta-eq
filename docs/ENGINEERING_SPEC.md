# Axell Penta EQ — engineering spec

The quality bar and the architecture. Tests in `tests/` implement §4; `PROGRESS.md` tracks status.

## 1. Signal flow

```
in ─ sanitize ─ input gain ─┬─────────────────────────── dry delay (= latency) ─────────────┐
                            │                                                               │
                            ├─ detectors (base rate, per dynamic band; input or SC bus)      │
                            │                                                               │
   Zero latency / Natural:  └─ [OS up] ─ bands 1..12 + HP/LP (matched SVF) ─ rack ─ [OS down]│
                               ─ correction FIR (min-phase | natural) ─┐                     │
   Linear phase:              (rack pre) ─ linear-phase FIR ─ dynamic deltas ─ (rack post)  │
                                                                        └─ auto gain ─ mix ─┘─ output gain ─ out
```
- **Rack** (character modules, fixed order Inductor → Console E/G → Passive Tube → Air), position
  pre or post the parametric EQ. Each module = linear analog sections + a nonlinear stage.
- **Bands**: 12 parametric + dedicated HP and LP. Types bell, low/high shelf, low/high cut,
  notch, band-pass, tilt. Placement Stereo / L / R / Mid / Side (mono: Side bands are inactive,
  everything else applies).
- **OS** 1x/2x/4x wraps the IIR band chain and the rack (nonlinear stages). Linear-phase FIR and
  correction FIRs run at the base rate.

## 2. Filter core

- Every band/module is a cascade of 1st/2nd-order **analog prototype sections**. Each section is
  realised by a **matched** digital biquad (impulse-invariant poles; Vicanek closed-form numerators
  for LP/HP/BP/peaking; 3-point DC/f0/Nyquist fit for shelves; 2-point for 1st order; matched-Z
  fallback) mapped onto a **TPT state-variable filter** with output mixing. Bell/shelf cuts are the
  exact reciprocal of the boost.
- Coefficients are recomputed every 16 base samples (aligned to absolute sample position →
  block-size invariant) and **linearly interpolated per sample** (SVF form → always stable).
- **Correction FIR** (zero latency): minimum-phase FIR (real cepstrum) of the residual
  `|analog| / |digital|` of the static chain, 256 taps at ≤ 50 kHz (512 ≤ 100 kHz, 1024 above).
  Recomputed at most every 512 samples while static parameters move; crossfaded; bypassed when the
  residual is < 0.002 dB everywhere.
- **Natural phase**: complex residual (`H_a/H_d · e^{−jωD}`) FIR, D = 64 samples × rate/48 k
  (rounded to a power of two) → analog magnitude **and** phase; latency D.
- **Linear phase**: FIR from the analog magnitude of the static chain (+ module residual),
  Kaiser window; Low 4096 / Medium 8192 / High 32768 taps × rate/48 k; uniformly partitioned
  overlap-save, P = N/32; latency N/2 + P. Designed on a background thread (synchronous in
  offline render and tests). Dynamic band movement = minimum-phase delta bands after the FIR.

## 3. Dynamics, character, global

- Dynamic band: detector = 2nd-order region filter of the source (Self / Band k region / External
  side-chain), power envelope with attack/release, soft knee 6 dB,
  `Δ = sign(range)·min(|range|, max(0, L−T)·(1−1/ratio))`. Dynamic is available for gain types
  (bell, shelves, tilt).
- Character band mode: proportional Q `Q·clamp(0.55+0.03|G|, 0.55, 1.45)`, non-reciprocal parallel
  form `1+(g−1)·BP` (bell) / `1+(g−1)·LP|HP` (shelves); the boost component saturates with Drive.
- Global: input gain, output gain, drive (0–100 %), mix (latency-aligned dry), static loudness
  auto gain (pink × K-weighted power of the analog curve), phase mode, linear-phase quality, OS,
  rack position. Smoothing on every continuous parameter (20 ms, log for frequency/Q).
- Stepped/structural changes (type, slope, mode, placement, add/remove, bypass) fade the band out
  (8 ms) → switch → fade in. Phase-mode / OS / quality changes fade the whole output (10 ms each way).

## 4. Quality gates (automated)

| # | Gate | Target |
|---|---|---|
| 1 | Clean magnitude vs analog prototype, every type, grid of f (30 Hz–19 kHz) × Q (0.3–12) × gain (±6/±18) × slope, 44.1/48/96 kHz, 20 Hz–20 kHz (≤ 0.95·Nyquist) | ±0.1 dB where target ≥ −24 dB (cuts: ±0.5 dB to −48 dB) in Zero Latency and Natural Phase on the full grid. Linear Phase is limited by FIR length: Medium is gated for f ≥ max(100 Hz, 40·Q) (notches ≥ 1 kHz, cuts ≥ 48 dB/oct from 300 Hz), High for f ≥ 10·Q (30–100 Hz set) |
| 2 | Flat + character off | null vs (delayed) input: bit-exact in Zero Latency; ≤ −120 dBFS residual in Natural/Linear |
| 3 | Fuzz: random extreme params, fast automation, all modes, all rates | no NaN/Inf, |out| < +24 dBFS for ≤ 0 dBFS in, no clicks (sample-to-sample step ≤ 6× the input's) |
| 4 | Block sizes 1/17/64/512/4096 | bit-identical output (Zero Latency / Natural) |
| 5 | Latency | impulse peak = reported latency in all modes, OS factors, rates |
| 6 | Dynamics | static Δ at known level within ±0.5 dB, attack/release time constants ±20 % |
| 7 | Stereo placement | L/R/M/S bands affect only their channel/component (other ≤ −120 dB) |
| 8 | Passive Tube | boost+atten produces LF bump and a dip 3–8× above the selected frequency |
| 9 | CPU | report per instance for vocal, beat and master presets (48 kHz/128, stereo) |
| 10 | State, presets, A/B, undo, latency reporting | `PentaPluginTests` |
| 11 | Steinberg validator + pluginval strictness 10 | pass |
