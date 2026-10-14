# Axell's measured voice profile → EQ targets

Measurements supplied by the user (mono, 48 kHz / 24-bit, foam-lined wardrobe booth):

| Measurement | Value | Interpretation | EQ response |
|---|---|---|---|
| F0 median | ~167 Hz | upper-baritone/tenor speaking pitch; first harmonic sits above the "chest" zone, so the voice reads thin | add weight *below* F0 (80–160 Hz) with a broad shelf/bell; keep HPF low (≈ 60–70 Hz) |
| Chest 80–160 Hz | weak | little energy under F0 | +2…+3.5 dB low shelf ~120–150 Hz, Q ~0.7 (or Passive Tube LF 100 Hz boost) |
| Boxy peak | 350–600 Hz | booth modes/early reflections (V8) | −3…−4 dB bell ~450 Hz, Q ~1.4; **dynamic** (only when it builds up) in the booth-fix preset |
| Presence | dip 2.5–5 kHz | consonants/intelligibility under-represented | *split* presence boost: two wide bells ~2.8 kHz and ~4.6 kHz (+1.5…+2.5 dB) instead of one big peak — fills the dip evenly without honk |
| Sibilance | peak ~7.8–9.6 kHz | s/sh energy concentrated high | dynamic bell ~8.7 kHz, Q ~2.5, range −6 dB, fast attack |
| Spectral tilt | −3.8 dB/oct | darker than a typical close-mic'd voice (≈ −3 dB/oct) | gentle tilt up / Air shelf (+2…+4 dB, 20 kHz setting) |
| Centroid | 1604 Hz | low-mid weighted (consistent with the tilt and boxiness) | the box cut + presence/air moves raise it towards ~2 kHz |

## Mic-specific adjustments

- **Shure SM7B** (dynamic, M1): darker top and strong proximity effect up close. Needs more presence
  and air than the condenser (+1 dB more on both presence bells, Air +3.5 dB @ 20 kHz), less low
  shelf (proximity already adds chest), HPF 70 Hz. Sibilance less aggressive (range −4.5 dB).
- **Lewitt LCT 440 Pure** (condenser, M2): already has a slight HF lift and captures more of the
  booth. More box cut (−4 dB), more chest (+3 dB shelf), modest presence, Air only +1.5 dB @ 40 kHz,
  stronger dynamic sibilance (range −7 dB).
- **Booth fix** (either mic): HPF 65 Hz, chest shelf +2.5 dB @ 140 Hz, **dynamic** box cut 450 Hz
  (static −1.5 dB, dynamic range −4 dB above threshold), split presence 2.8 k / 4.6 k, dynamic
  sibilance 8.7 kHz, Air +2.5 dB @ 20 kHz.

These are starting points: the user signs off by ear (see the listening checklist in README).
