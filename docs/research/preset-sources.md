# Preset sources

Every factory preset (Source/Presets.h) and where its moves come from. IDs refer to
[engineering-practice.md](engineering-practice.md) (V = vocals, M = microphones, B = beats/instruments,
X = mix bus/master), [voice-profile.md](voice-profile.md) (VP) and [reference-eqs.md](reference-eqs.md) (R1–R5:
the five reference EQs, module models). Measured curves: [docs/preset-curves](../preset-curves/README.md).

All presets are loudness-matched to bypass within ±0.5 LU on pink noise (K-weighted, DspTests section P), so A/B
comparisons judge tone, not level. Dynamic thresholds assume a typical tracked level (vocal peaks around −6 dBFS,
−18 dBFS RMS); adjust THRESH if your level differs.

| Preset | Category | Moves | Sources |
|---|---|---|---|
| Init (Flat) | Utility | flat | — |
| Rap Lead - Clean & Forward | Vocals | HP 80 Hz 24 dB/oct; +1.5 @ 250 Hz; −2.5 @ 400 Hz Q 1.4; +2 @ 3.5 kHz; dynamic de-ess 7.5 kHz (−5 dB); +2 dB shelf 12 kHz | V1, V3, V2, V4, V5, V6 |
| Boom Bap Lead - Warm Grit | Vocals | HP 90; −2 @ 500; dynamic 6.5 kHz; Inductor 110 Hz +2, 3.2 kHz +3, 12 k +1.5; drive 45 % | V1, V2, V5; R3 (inductor mid "forward without harshness", transformer density) |
| Dark & Gritty Lead | Vocals | HP 100; Character +2.5 @ 1.5 kHz; +1.5 shelf 200; LP 9 kHz; Console G HMF 2 kHz +3; drive 60 % | V3, V4 (mid-forward instead of bright), B6 (band-limited top for dusty beats), R4 (G proportional Q) |
| Doubles - Tuck Behind | Vocals | HP 150; −2 shelf 250; −1.5 @ 3 kHz; deeper de-ess; −1.5 shelf 10 k | V9, V5 |
| Ad-libs - Hype Cut | Vocals | HP 200; +3 @ 1.2 kHz; +2 @ 5 kHz; +1 shelf; LP 14 kHz | V9 (separation from the lead), V4 |
| Backing Stack - Wide & Soft | Vocals | HP 180; −3 shelf 300; Mid −2 @ 3 kHz; Side +2.5 shelf 9 kHz | V9, X3 (side air for width) |
| Telephone / Lo-fi | Vocals | HP 400 / LP 3.4 kHz (24 dB/oct); +6 @ 1.5 kHz Q 2; +3 @ 800 Hz Q 4; Console drive 80 % | V7 |
| De-ess + Air | Vocals | dynamic 6.5 kHz (−6) and 9 kHz (−4), fast attack; Air 20 kHz +4 | V5, V6; R5 |
| Podcast / Spoken Word | Vocals | HP 90; +1 shelf 120; −2.5 @ 300; +2 @ 4 kHz; dynamic 7 kHz | V10 |
| Chorus / Hook Lift | Vocals | HP 100; Character +2 @ 2 kHz; +2 shelf 9 k; −1 shelf 200; Passive Tube high boost 10 kHz broad | V4, V6; R2 (broad passive high boost) |
| Harsh Mic Tamer | Vocals | dynamic 3 kHz (−5) and 5.5 kHz (−4); −1.5 @ 8 kHz | V4/V5 (harshness lives 2–5 kHz, sibilance above), X7 |
| Room Box Removal | Vocals | narrow cuts 280 / 450 / 650 Hz; +1 shelf 10 k | V8, V2 |
| Rap Lead - Passive Tube Polish | Vocals | parametric HP + box cut + de-ess; Passive Tube 100 Hz boost 3/atten 2, 10 kHz boost 4 broad, 20 kHz atten 2; drive 35 % | V1, V2, V5; R2 (low trick, broad top, tube makeup) |
| Vocal - Spectral Harshness Tamer | Vocals | HP 80; +1 @ 250; spectral 3.2 kHz Q 0.7 (thr −30, −6, fast) and spectral high shelf 6.5 k (thr −32, −7, 1 ms) | V4 (presence region 3–5 kHz; harshness is its excess), V5 (sibilance: a dynamic band), X7 (2–5 kHz harshness); workflow-study.md §1 |
| Kick - Punch & Knock | Beats | HP 30; +3 @ 60 Hz; −4 @ 400 Hz; +3 @ 3.5 kHz; LP 12 kHz | B1 |
| Snare - Crack & Body | Beats | HP 80; +2 @ 200; −2 @ 900; +3 @ 5 kHz; +2 shelf 10 k | B2 |
| Hats - Smooth Sizzle | Beats | HP 300; dynamic 3 kHz; −1 @ 7 kHz; +2 shelf 10 k | B3 |
| 808 - Clean Sub + Bite | Beats | HP 25; +2 @ 50 Hz; Character +2.5 @ 120 Hz with drive 60 % (harmonics); −3 @ 300; LP 8 kHz | B4, B5 |
| Bass - Defined Low End | Beats | HP 30; +2 shelf 80; −3 @ 250; +2 @ 800; LP 5 kHz | B8 (mud zone), B4 |
| Dusty Sampler (12-bit era) | Beats | HP 60; LP 11 kHz 24 dB/oct; Character +2 @ 1.8 kHz; Inductor low/mid push, drive 70 % | B6; R3 |
| Vinyl Loop Cleanup | Beats | HP 35 48 dB/oct; Side low cut 150 Hz; dynamic 9 kHz (crackle); −1.5 @ 250 | B7, X2 |
| Keys / Rhodes - Warm Bell | Beats | HP 80; −2 @ 250; +2 @ 2 kHz; +1 shelf 8 k; Passive Tube 60 Hz boost 2/atten 1 | B8; R2 |
| Guitar - Clean Up & Presence | Beats | HP 90; −2 @ 250; +2.5 @ 3 kHz; LP 14 kHz | B8 |
| Drum Bus - Boom Bap Thump | Beats | HP 25; Passive Tube 60 Hz boost 4/atten 2; Console E LMF 400 −2, HMF 3 k +2; drive 50 % | X5; R2, R4 (E: punchy constant bandwidth) |
| Beat Bus - Glue Polish | Beats | HP 22; +1 shelf 60; −1 @ 300; +1 @ 3 k; +1.5 shelf 10 k; Console G drive 40 % | X1; R4 |
| Bass/Kick Carve (side-chain) | Beats | dynamic 60 Hz −6 dB and 3.5 kHz −3 dB keyed by the external side-chain (kick) | B4 |
| Sample Chop - Room for Vocals | Beats | HP 40; Mid 2.5 kHz −1 static, −4 dynamic keyed by the vocal side-chain | V4 (vocal presence region), B4 (side-chain EQ technique) |
| Master - Gentle Polish | Mix/Master | Natural Phase; HP 20; +0.8 shelf 60; −0.7 @ 300; +0.5 @ 3 k; +1 shelf 12 k | X1 |
| Master - Boom Bap Warmth | Mix/Master | Natural Phase; Passive Tube 30 Hz boost 3/atten 2, 12 kHz boost 2 broad; Console E drive 30 % | X5; R2, R4 |
| Master - Air & Clarity | Mix/Master | Linear Phase; −0.8 @ 250; +0.7 @ 4 k; Air 40 kHz +3 | X1, X4; R5 |
| Master - Low-End Control (dynamic) | Mix/Master | Natural Phase; HP 22; dynamic 60 Hz (−3) and 150 Hz (−2), slow | X6 |
| Master - M/S Widen Highs | Mix/Master | Linear Phase; Side +2 shelf 8 k; Side low cut 120; Mid −0.5 @ 300 | X3, X2, X4 |
| Master - Mono Bass | Mix/Master | Linear Phase; Side low cut 120 Hz 24 dB/oct; Mid +0.5 shelf 80 | X2, X4 |
| Master - De-Harsh | Mix/Master | Natural Phase; dynamic 3 kHz (−3), 6 kHz (−2); −0.5 dB tilt | X7, X1 |
| Master - Loudness Prep | Mix/Master | Linear Phase High; HP 28 Hz 48 dB/oct; Side low cut 100; −0.5 @ 150; dynamic 3.5 kHz | X7, X2 |
| Mix Bus - Console Glue | Mix/Master | Console G LF 60 +1.5, HMF 3 k +1, HF 12 k +1; drive 50 % | R4 (G proportional Q, console drive) |
| Mix Bus - Spectral Resonance Control | Mix/Master | Spectral 320 Hz Q 0.8 (thr −28, −3, ratio 3, slow) and 2.8 kHz Q 0.6 (thr −30, −4); +1 shelf 12 k | V2, V8 (low-mid ringing / boxiness 200–500 Hz), X7 (2–5 kHz harshness), X1 (small, wide, invisible moves); workflow-study.md §1 |
| Axell Signature - LCT 440 Pure | Axell Signature | HP 65; +3 shelf 140; −4 @ 450 Q 1.4; +1.5 @ 2.8 k and 4.6 k; dynamic 8.7 kHz (−7, fast); Air 40 kHz +1.5 | VP (all rows), M2 |
| Axell Signature - SM7B | Axell Signature | HP 70; +2 shelf 140; −3 @ 450; +2.5 @ 2.8 k and 4.6 k; dynamic 8.7 kHz (−4.5); Air 20 kHz +3.5 | VP, M1 |
| Axell Signature - Booth Fix | Axell Signature | HP 65; +2.5 shelf 140; 450 Hz −1.5 static / −4 dynamic; +2 @ 2.8 k and 4.6 k; dynamic 8.7 kHz (−6); Air 20 kHz +2.5 | VP, V8 |
| Axell Signature - Tracking (Light) | Axell Signature | zero-latency light version: HP 70; +2 shelf 140; −3 @ 450; +2 @ 3.6 k; dynamic 8.7 kHz | VP; Zero Latency for monitoring |

Notes
- Microphone model names in the two Signature presets identify the microphone the preset was tuned for
  (nominative use); there is no affiliation with the manufacturers.
- "12-bit era" refers to the general late-1980s sampler sound described in B6, not to a specific product.
