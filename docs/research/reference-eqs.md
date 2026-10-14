# Reference equalisers — design study

Internal research document. Brand and product names appear **only here** (and in other internal
research docs) to identify what was studied. They never appear in the plugin UI, mode names or
preset names (see `clean-room.md`). Everything in the plugin is modelled from published circuit
descriptions, published specifications/measurements and DSP theory — no code, graphics, impulse
responses or data were taken from any commercial product.

## Verdict on the shortlist

The proposed top five are confirmed. Together they span the four useful "families" of EQ
behaviour an all-rounder needs:

| # | Reference | Family it represents | What we take from it | Becomes in the plugin |
|---|---|---|---|---|
| 1 | FabFilter Pro-Q 4 | modern surgical digital EQ | workflow (drag-on-analyzer), band types, slopes, dynamic EQ, M/S per band, phase modes, auto gain, solo | the parametric core (12 bands + HP/LP) |
| 2 | Pultec EQP-1A | passive LC + tube makeup | interacting LF boost/attenuate, broad HF peak with bandwidth, HF atten shelf, push-pull tube colour | **Passive Tube** module |
| 3 | Neve 1073 | inductor/transformer class-A channel | fixed musical frequencies, resonant LF shelves, gentle 12 k shelf, 18 dB/oct HPF, transformer LF saturation | **Inductor** module |
| 4 | SSL 4000 E / G | VCA-era console channel EQ | constant-bandwidth (E) vs proportional-Q (G), G shelf overshoot, bell/shelf switches, subtle console drive | **Console E/G** module |
| 5 | Maag EQ4 | ultra-wide "air" shelf | boost-only, very wide shelf whose corner can sit far above the audio band (2.5–40 kHz) | **Air** module |

Secondary references and what they contributed:

- **API 550A** — proportional Q ("bandwidth narrows as gain increases") with reciprocal
  boost/cut; used as the definition of *proportional Q* in the per-band **Character** mode and in
  Console G.
- **Manley Massive Passive** — passive, interacting bands with tube makeup; every band shelf or
  bell. Confirms that interaction between passive bands is a *feature* engineers seek; reinforces
  the Passive Tube interaction model.
- **Sonnox Oxford EQ** — documents the useful distinction between *reciprocal* curves (boost and
  cut mirror images) and *non-reciprocal* curves (cuts tighter than boosts). Our **Clean** bells
  are reciprocal; **Character** bells are non-reciprocal (cut narrower than boost).
- **TDR Nova** — parallel dynamic EQ architecture: dynamic bands as `x + (g-1)·BP(x)` so the gain
  can move per-sample without redesigning poles. Used for Character-mode bands (parallel form).

Not added: spectral dynamics (Pro-Q 4) — out of scope for v1, noted as a future feature.

---

## 1. FabFilter Pro-Q 4 (modern digital reference)

**Philosophy.** A transparent, "analog-matched" parametric EQ whose curves behave like ideal
analog prototypes (no bilinear cramping), wrapped in a fast graphical workflow: click on the
spectrum to create a band, drag to move it, scroll for Q, per-band dynamics, per-band M/S/L/R.
Up to 24 bands; "universal slope support for all EQ shapes"; dynamic EQ with external side-chain;
spectral dynamics; EQ sketch; instance list; vintage saturation.

**Phase modes** (FabFilter help, *Processing mode*):

- *Zero latency*: no delay, matches analog **magnitude** but "minor magnitude deviations above
  18 kHz" and non-analog phase.
- *Natural phase*: "perfectly matches magnitude response" and "closely matches the analog phase
  response"; negligible deviation above 20 kHz.
- *Linear phase*: latency by resolution — Low 3072, Medium 5120, High 9216, Very High 17408,
  Maximum 66560 samples at 44.1 kHz. Higher resolution = better LF accuracy but more pre-echo
  risk. Dynamic EQ is available in linear phase only up to "High".
- Pro-Q's own guidance: linear phase is "a problem-solving tool", not inherently better.

**Curve shapes.** Bell, low/high shelf, low/high cut, notch, band pass, tilt shelf, flat tilt,
all with selectable slopes (6–96 dB/oct for cuts, "brickwall" option). Q for cuts gives a
resonant peak at the corner.

**Why engineers love it.** Speed (drag on the analyzer), honesty (curve = sound), precision,
mid/side per band, dynamic bands replace a de-esser + multiband compressor in many cases, and
auto gain makes A/B comparisons fair.

**Typical uses.** Surgical resonance removal, de-essing / de-harshing via dynamic bands, M/S
mastering moves, matching, steep filters, transparent tone shaping.

**What we implement.** 12 bands + dedicated HP/LP, all core shapes incl. tilt, slopes 6–96 dB/oct,
resonance on cuts, dynamic per band (threshold, range, ratio, attack, release, source), Stereo/L/R/M/S
placement, three phase modes, auto gain, solo/listen, pre/post/sidechain analyzer.

Sources: [FabFilter Pro-Q help: processing mode](https://www.fabfilter.com/help/pro-q/using/processingmode),
[Pro-Q 4 product summary (Plugin Boutique)](https://www.pluginboutique.com/products/14125-FabFilter-Pro-Q-4).

---

## 2. Pultec EQP-1A (passive LC program equaliser, 1951/1961)

**Topology.** An **all-passive LC network** (inductors, capacitors, resistors; a multi-tapped
inductor for the HF boost) sandwiched between transformers, followed by a **push-pull tube makeup
amplifier** (12AX7 input, 12AU7 output, 6X4 rectifier) with negative feedback from the output
transformer. The passive network loses roughly the gain the tube stage restores. (SOS review of
a faithful copy: input, inter-stage and output transformers; push-pull balanced amp.)

**Controls and published ranges** (Pulse Techniques / Wikipedia / SOS):

| Section | Frequencies | Range |
|---|---|---|
| LF boost | 20, 30, 60, 100 Hz | 0 … +13.5 dB (≈ +14) |
| LF atten | same selector | 0 … −17.5 dB (≈ −18) |
| HF boost | 3, 4, 5, 8, 10, 12, 16 kHz | 0 … +18 dB, with **bandwidth** (sharp–broad) |
| HF atten | 5, 10, 20 kHz | 0 … −16 dB (shelf) |

**Curve shapes.**
- LF boost and LF atten used alone are *gentle shelves* — SOS: "a gentle shelf EQ boost (up to
  about 14 dB) or cut (down to about −18 dB) with something approaching a modest 3 dB/octave slope".
- The **attenuation curve extends higher in frequency than the boost curve** for the same
  selector position (SOS; UPF/MTG thesis summary). Boost and atten are separate networks with no
  buffering between them, so they interact.
- **Simultaneous boost + atten ("the trick")**: "a characteristic 'resonant shelf' response with
  a mid-frequency dip (up to 8 dB deep) before the LF shelf boost starts to build" (SOS). The UPF
  thesis summary gives an example at the 30 Hz setting: a low boost with a dip "at around 200 Hz".
  Engineers describe it as "the bottom gaining mass while the band immediately above it loses the
  excess that normally clouds kick and bass".
- HF boost is a **peaking** (bell) response, bandwidth knob changes Q; HF atten is a **shelf**.
- Boosts are broad and smooth; resonance from the LC network gives the curves a slightly
  "rounded", non-textbook shape.

**Nonlinearities.** The tube makeup stage is push-pull, so even harmonics largely cancel —
reviews note a "mild tendency towards third-harmonic distortion". Transformers add LF-dependent
saturation at high level. At normal levels the unit is nearly clean; its colour is mostly the
curve shapes.

**Why loved.** Broad, forgiving boosts that "can't sound bad"; the LF trick gives weight without
mud; HF boost with broad bandwidth gives silky top. Classic on kick, bass, vocals, mix bus.

**Our model ("Passive Tube").** Clean-room behavioural model calibrated to the published facts
above (exact component values are not used):
- LF boost: 2nd-order low shelf with low Q (resonant LC character, slight bump at the corner),
  corner ≈ selected frequency, gain law 0–10 → 0 … +13.5 dB.
- LF atten: 1st-order shelf with corner placed **higher** than the boost corner (≈ 2.5× the
  selected frequency), 0–10 → 0 … −17.5 dB. Product of the two gives the resonant-shelf + dip
  behaviour; tests check that the dip appears 3–8× above the selected frequency and deepens with
  atten.
- HF boost: bell at the selected frequency, bandwidth 0–10 maps Q ≈ 2.5 (sharp) … 0.45 (broad),
  0 … +18 dB.
- HF atten: 1st/2nd-order high shelf at 5/10/20 kHz, 0 … −16 dB.
- Tube stage: push-pull style odd-symmetric soft saturation (3rd-harmonic dominant) whose amount
  follows the global Drive, plus a very gentle output-transformer LF/HF bandwidth (−0.5 dB at
  ~12 Hz and ~30 kHz).

Sources: [SOS — Cartec EQP1A review (H. Robjohns, Oct 2010)](https://www.soundonsound.com/node/4907989),
[SOS — Pulse Techniques EQP-1A review](https://www.soundonsound.com/reviews/pulse-techniques-eqp-1a),
[Wikipedia — Pultec EQP-1](https://en.wikipedia.org/wiki/Pultec_EQP-1),
[Pulse Techniques EQP-1A](https://www.pulsetechniques.com/products/eqp-1a),
[EQP-WDF-1A — UPF/MTG master's thesis project (A. Barrera)](https://github.com/ABSounds/EQP-WDF-1A) (read for its
circuit findings only; no code used),
[Puremix — the low-end trick](https://www.puremix.com/blog/fab-dupont-pultec-low-end-trick),
[Abbey Road Institute — demystifying the Pultec](https://abbeyroadinstitute.nl/blog/demystifying-the-pultec/).

---

## 3. Neve 1073 (class-A inductor channel, 1970)

**Topology.** Discrete class-A amplifiers with input/output transformers (Marinair) and an
**inductor-based** EQ: inductors + switched capacitors form resonant networks, so each frequency
position has its own curve character. HPF is passive and uses magnetics, third order.

**Controls** (AMS Neve, SOS):

| Band | Frequencies | Range | Shape |
|---|---|---|---|
| HPF | 50, 80, 160, 300 Hz | — | 18 dB/oct (3rd order) |
| Low | 35, 60, 110, 220 Hz | ±16 dB | shelf, *resonance differs per position* |
| Mid | 360, 700, 1.6 k, 3.2 k, 4.8 k, 7.2 k | ±18 dB | peaking, fixed Q (no Q control); Q is higher at higher frequencies and the bandwidth depends on gain |
| High | 12 kHz fixed | ±16 dB | shelf with a **very gradual slope** (poles/zeros interleaved) |

(Published ranges vary between ±15 and ±18 dB by source/version; we use ±16 shelves, ±18 mid.)

**Nonlinearities.** Transformer saturation is **flux-dependent**: core flux is the time integral
of voltage, so low frequencies saturate first and produce mostly odd (3rd) plus some 2nd
harmonics (asymmetry/hysteresis). The class-A gain stages add 2nd harmonic. Colour rises steeply
with level ("drive the input").

**Why loved.** Musical fixed frequencies (hard to make it sound bad), thick resonant low end,
smooth 12 k "air" shelf, mids that sound forward without harshness; the transformers' LF density.

**Our model ("Inductor").** HPF 3rd-order Butterworth (off/50/80/160/300 Hz); low shelf as a
2nd-order resonant shelf with per-position Q (35 Hz → 0.9 … 220 Hz → 0.6, i.e. more resonance at
the lowest positions); mid as a bell whose Q rises with frequency (0.7 @ 360 Hz … 1.4 @ 7.2 kHz)
and mildly with |gain| (proportional); high as a **1st-order** (gentle) 12 kHz shelf; transformer
stage = flux-weighted saturation (LF-emphasised odd + small even component) scaled by Drive.

Sources: [AMS Neve 1073LBEQ](https://ams-neve.com/1073lbeq/),
[SOS Plug-in Folder (Walden & White, Nov 2006)](https://www.soundonsound.com/node/4911866),
[Audio consoles wiki — Neve 1073](https://audioconsoles.fandom.com/wiki/Neve_1073),
UA modelling notes (2006 webzine, quoted in search results: "low shelves have varying degrees of
resonance as different capacitors are switched in … the high shelf has a very gradual slope …
because its poles and zeros are interpolated").

---

## 4. SSL 4000 E / G channel EQ (1979 / 1987)

**Topology.** Active op-amp (later VCA-console era) 4-band EQ with HPF/LPF. Two famous
variants: the E-series (02 "brown", then 242 "black knob") and the G-series (383/292).

**Behaviour** (Waves blog, Abbey Road Institute):
- **E**: band bandwidth is **constant regardless of gain** — "slightly more presence and edge".
  Black-knob 242: ±18 dB, 18 dB/oct HPF; LF/HF shelves switchable to bell.
- **G**: **proportional Q** — the more boost/cut, the narrower the Q, "keeping the overall energy
  change relatively consistent"; steeper filters; shelves/bells have **overshoot** — "a pre-boost
  dip and a pre-cut rise"; extra LMF ÷3 / HMF ×3 range switches.
- Typical ranges (both): LF 30–450 Hz, LMF 200 Hz–2.5 kHz, HMF 600 Hz–7 kHz, HF 1.5–16 kHz,
  Q ≈ 0.5–2.5 on the mid bands; HPF 16–350 Hz, LPF 22–3 kHz.

**Nonlinearities.** Clean at normal level; the "console sound" at higher levels is op-amp/VCA
soft clipping — mostly odd harmonics — and it is part of the "punch" on drums/bus.

**Why loved.** Fast, aggressive, "punchy" mid boosts on drums and vocals (E), smooth broad musical
moves at low gain and surgical at high gain (G).

**Our model ("Console E/G").** 4 bands + HPF (2nd/3rd order) + LPF (2nd order). **E**: Q fixed at
the knob value for all gains, shelves are non-resonant (Q 0.707). **G**: Q_eff = Q·(0.55 + 0.03·|G|)
clamped (wide at low gain, narrow at high gain — the API-style proportional law) and shelves with
overshoot (shelf Q 1.0–1.2 → "pre-boost dip / pre-cut rise"). Bell switches on LF/HF. Console
drive: odd-dominant soft clip, Drive-scaled.

Sources: [Waves — SSL E-Channel or G-Channel? (2021)](https://www.waves.com/ssl-e-channel-or-g-channel),
[Abbey Road Institute — SSL E vs G](https://abbeyroadinstitute.nl/blog/ssl-e-g-series-eq/),
[Waves — API 550 vs 560](https://www.waves.com/api-550-or-api-560-the-differences-explained),
[SOS API 550A review](https://www.soundonsound.com/node/4911270).

---

## 5. Maag EQ4 (AIR BAND)

**Topology.** Low-phase-shift 6-band design with interacting (summing) band-passes:
Sub (10 Hz), 40 Hz, 160 Hz, 650 Hz, 2.5 kHz shelf (+15/−4.5 dB) and the **AIR BAND**:
*boost-only* shelf at 2.5, 5, 10, 20 or 40 kHz, up to **+20 dB**. Manual: "The frequencies below
the shelf peaks are also affected due to the transitional slope" — i.e. the shelf is extremely
wide; with the 40 kHz setting, the audible band only sees the bottom of the transition, giving a
smooth lift of the top octave without a harsh "corner". Phase shift "very minimal"; bands interact
(the AIR gain raises overall level slightly).

**Why loved.** Adds openness/"air" without harshness or sibilance emphasis, because the energy
added rises slowly and keeps rising above the audible band rather than peaking at 10–12 kHz.

**Our model ("Air").** A **1st-order** (6 dB/oct) high shelf whose corner (−3 dB-of-gain point)
is the selected 2.5/5/10/20/40 kHz, Air gain 0 … +20 dB. The analog prototype is evaluated even
when the corner is above Nyquist (matched design + correction match it exactly up to Nyquist),
so the 20/40 kHz settings give the expected gentle tilt-up of the top octave at 44.1/48 kHz.

Sources: [Maag EQ4 user guide (PDF)](https://www.audiosteps.io/wp-content/uploads/Maag-Audio-EQ4-User-Guide.pdf),
[SOS — Maag EQ4M review](https://www.soundonsound.com/reviews/maag-audio-eq4m).

---

## Secondary references

### API 550A
Three/four bands of switched frequencies, ±12 dB (550A) in 2/3 dB steps, **proportional Q** and
reciprocal boost/cut ("cutting by an equal amount brings you back to where you started"). Claimed
to minimise "phase-shift sound" and to be hard to make sound bad at extreme settings.
→ defines our proportional-Q law. Sources: [Waves API 550/560](https://www.waves.com/api-550-or-api-560-the-differences-explained),
[SOS API 550A](https://www.soundonsound.com/node/4911270).

### Manley Massive Passive
2-channel, 4-band **passive** tube EQ, every band switchable bell/shelf, the bandwidth control
also shapes the shelves (overshoot), all-tube makeup, interacting "parallel symmetrical"
topology. → supports passive interaction modelling and the overshoot/bandwidth on shelves.
Source: [Manley — Massive Passive](https://www.manley.com/products/pro-audio/eqs/massive-passive-eq).

### Sonnox Oxford EQ
Four bell "types": Type 1 reciprocal (80s US console), Type 2 non-reciprocal (cuts tighter),
Type 3 English channel curves, Type 4 wide mastering curves; shelves identical across types.
→ reciprocal Clean bells vs non-reciprocal Character bells.
Source: [UA blog — Sonnox Oxford EQ tips](https://uaudio.de/blog/sonnox-eq-tips-tricks).

### TDR Nova
Parallel dynamic EQ (all bands in parallel except input HP/LP), "provokes interesting band
interactions and can severely reduce the amount of side-effects produced by nonlinear operations".
→ our Character bands use the parallel form `x + (g−1)·BP(x)`, which lets the dynamic gain move
per sample and lets the band's boost component saturate on its own.
Source: [TDR Nova manual](https://docs.tokyodawn.net/nova-manual/).
