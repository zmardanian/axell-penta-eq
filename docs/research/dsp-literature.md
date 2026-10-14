# DSP literature and design decisions for the filter core

## Sources studied

| Topic | Source | Used for |
|---|---|---|
| Biquad EQ prototypes | R. Bristow-Johnson, *Audio EQ Cookbook* — [W3C note](https://www.w3.org/TR/audio-eq-cookbook/) | analog prototypes of bell, shelves (with Q/"S"), notch, band-pass; Q definitions |
| Matched (de-cramped) biquads | M. Vicanek, *Matched Second Order Digital Filters*, 2016 — [PDF](https://www.vicanek.de/articles/BiquadFits.pdf) | impulse-invariant poles + magnitude-matched numerator via the `B0φ0+B1φ1+B2φ2` form; closed forms for LP/HP/BP/peaking; minimum-phase factorisation |
| Prescribed Nyquist gain | S. J. Orfanidis, *Digital Parametric Equalizer Design with Prescribed Nyquist-Frequency Gain*, JAES 45(6), 1997 — [PDF](https://www.ece.rutgers.edu/~orfanidi/ece348/peq.pdf) | why bilinear EQs cramp; the constraint `f0 < (1−1/(2Q))·f_Ny` of closed-form Nyquist matching |
| Analog-matched low-pass | M. Massberg, AES 131st Conv. 2011 | alternative LP matching (not used; Vicanek forms are simpler) |
| VA filters / TPT SVF | V. Zavalishin, *The Art of VA Filter Design* rev. 2.1 — [PDF](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf); A. Simper, *Linear trapezoidal SVF* — [PDF](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf) | modulation-robust realisation of every 2nd-order section (state-variable form, any biquad mapped to SVF + output mix) |
| Minimum-phase from magnitude | Oppenheim & Schafer, *Discrete-Time Signal Processing*, ch. 12 (complex cepstrum / homomorphic min-phase) | zero-latency correction FIR |
| Linear-phase FIR EQ | frequency-sampling design + window; uniformly partitioned overlap-save convolution (Stockham; Gardner, JAES 1995) | Linear Phase mode |
| Pre-ringing trade-off | FabFilter help — [processing mode](https://www.fabfilter.com/help/pro-q/using/processingmode) | resolution options, "linear phase is a problem-solving tool" |
| Oversampling nonlinear stages | Duo Vocal Comp `Oversampling.h` (our own, measured ≥100 dB image rejection); standard aliasing theory (a memoryless nonlinearity of order *n* widens bandwidth *n*×) | 1x/2x/4x for drive stages |
| Dynamic EQ | TDR Nova manual (parallel dynamic structure); McDSP — [de-essing with dynamic EQ](https://mcdsp.com/2021/08/25/blog-2-how-to-use-dynamic-eq-for-vocal-de-essing/); Pro-Q help | threshold/range/ratio semantics, detector filtering, external side-chain |
| Loudness weighting | ITU-R BS.1770 — [rec.](https://www.itu.int/rec/R-REC-BS.1770) | K-weighted static auto-gain estimate |
| Pultec measurement studies | SOS reviews (Robjohns 2010), UPF/MTG thesis (WDF model, LTSpice validation) | Passive Tube calibration targets (see `reference-eqs.md`) |

## Why plain bilinear (RBJ) is not good enough

The bilinear transform maps the whole analog axis 0…∞ onto 0…Nyquist. A bell at 10 kHz at
44.1 kHz gets visibly narrower and a high shelf gets steeper than the analog curve ("cramping").
Pro-Q and every modern "analog-matched" EQ avoid it. Requirement for this project: **Clean mode
must match the analog prototype within ±0.1 dB** at 44.1/48/96 kHz.

## Prototype study (tools/proto/*.py) — measured, not assumed

Grid: f0 20 Hz–20 kHz (25–41 log steps), Q 0.1–18, gain 1–30 dB, error measured 20 Hz–min(20 kHz,
0.95·Nyquist) against the analog prototype.

1. **Single matched biquad alone is not enough.** Best-of {Vicanek peaking, 3-point
   (DC / f0 / Nyquist), weighted least squares} still failed ±0.1 dB on 15–25 % of a realistic grid
   at 44.1/48 kHz (worst ≈ 0.8–1.2 dB for bells, up to 3.8 dB for resonant high shelves). At 96 kHz
   Vicanek bells pass everywhere (worst 0.059 dB) but shelves still fail (worst 1.4 dB).
   Failures are of two kinds: (a) wide or high bands whose analog response still moves above
   0.9·Nyquist — a digital response must have zero slope at Nyquist, so a 2nd-order section cannot
   follow; (b) resonant shelves (Q > 1) whose overshoot shape a 3-constraint numerator cannot follow.
2. **Matched-Z transform** (poles and zeros via `exp`) is accurate at low frequencies only
   (first failures at 532 Hz for bells at 44.1 kHz).
3. A **4th-order fixed-pole least-squares fit** was badly conditioned and not better (14 % fail).
4. **Matched biquad + minimum-phase correction FIR (adopted).** Compute the residual
   `R(ω) = |H_analog(jω)| / |H_digital(e^jω)|` of the whole static EQ on a 2048-bin grid, turn
   `ln R` into a minimum-phase FIR via the real cepstrum (fold the cepstrum, exponentiate), keep
   256 taps (half-Hann tail). Result on the same hard cases:

   | Case (44.1 kHz) | matched biquad alone | + 256-tap min-phase correction |
   |---|---|---|
   | bell 893 Hz Q 0.3 +18 dB | 0.247 dB | 2.6e-7 dB |
   | bell 5971 Hz Q 0.3 +18 dB | 1.207 dB | 1.6e-6 dB |
   | low shelf 2518 Hz Q 1.5 +18 dB | 0.241 dB | 1.1e-7 dB |
   | high shelf 2518 Hz Q 1.5 +18 dB | 0.550 dB | 1.1e-7 dB |
   | bell 15 kHz Q 2 +12 dB | 0.724 dB | 2.4e-6 dB |
   | bell 20 kHz Q 8 +18 dB | 0.500 dB | 1.1e-5 dB |

   FIR length sweep (worst over a grid at 44.1/48 kHz): 16 taps 1.45 dB, 32 → 0.54, 64 → 0.19,
   128 → 0.062, **256 → < 0.0001 dB**. A minimum-phase FIR has no pre-delay, so **Zero Latency
   stays zero latency** while the magnitude matches the analog prototype. The correction is
   bypassed entirely when the residual is below 0.002 dB everywhere (flat EQ = bit-transparent).

Consequences for the architecture:

- Every band is a cascade of 1st/2nd-order **analog sections**. Each section gets a matched
  digital realisation (Vicanek closed forms for LP/HP/BP/peaking, 3-point fit for shelves,
  first-order 2-point fits; matched-Z as a guaranteed-valid fallback). Cuts (negative gain) of
  bells/shelves are the exact **reciprocal** of the boost, so only boosts are fitted.
- Every digital section is realised as a **TPT state-variable filter** with output mixing
  (any stable biquad maps exactly onto `g, k, m0, m1, m2`), so per-sample coefficient
  interpolation is stable and click-free — required for smooth automation and dynamic bands.
- **Correction FIR** (min-phase, 256 taps × rate/48 k) runs at the base rate after the band chain.
  It is recomputed (≈0.1–0.2 ms) at most every 512 samples while static parameters move; dynamic
  gain movement is not included (the matched biquad alone is within ~0.3 dB during movement).
- **Natural phase** = same core + a *complex* correction FIR (`H_analog/H_digital · e^{−jωD}`,
  D = 64 samples at 48 kHz) that also restores the analog phase — small latency, reported.
- **Linear phase** = FIR designed from the analog magnitude of the static curve (frequency
  sampling + window), uniformly partitioned FFT convolution; dynamic gain movement is applied as
  a minimum-phase delta band (documented trade-off; Pro-Q also restricts dynamics in high
  linear-phase resolutions).

## Proportional Q law

API/SSL-G style: bandwidth narrows as |gain| increases. Clean-room law (our own):
`Q_eff = Q · clamp(0.55 + 0.03·|G_dB|, 0.55, 1.45)` → at ±3 dB the band is ~1.6× wider than the
knob says, at ±15 dB it matches the knob, at ±30 dB it is ~1.45× narrower. Non-reciprocal
Character bells use the parallel form `1 + (g−1)·BP_Qeff(s)` so a cut is narrower than a boost.

## Oversampling

Nonlinear stages (Character-band drive, Passive Tube / Inductor / Console stages) create harmonics
up to n×f. Reused Duo Vocal Comp's measured linear-phase polyphase halfbands (≥100 dB image
rejection, exact integer latency 69/77 samples at 2x/4x). 1x keeps zero latency; the drive curves
are low-order (tanh / cubic-family) so aliasing at 1x is modest at sensible drive.

## Dynamic EQ design

- Detector: band-limited version of the source (own band region, another band's region, or the
  external side-chain), 2nd-order band-pass (bells), low/high-pass (shelves/cuts) at the band
  frequency; peak-RMS envelope (dB domain) with attack/release.
- Gain computer: soft knee 6 dB; `Δ = sign(range)·min(|range|, (L−T)·(1−1/ratio))` above
  threshold. Static gain + Δ is the instantaneous band gain; band redesigned every 16 samples with
  per-sample SVF interpolation.
- Stereo: Stereo-placed bands link detectors (max), L/R/M/S bands detect on their own channel.
- In Linear Phase, the detector reads the input delayed by the FIR latency so gain changes line
  up with the audio.
