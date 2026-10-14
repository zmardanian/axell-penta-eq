# Axell Penta EQ - DSP test report

Build: Release. Runtime 201 s.

**Result: ALL PASS** - 743 checks passed, 0 failed (every check runs once per sample rate / channel configuration).

## Summary (worst case over all configurations)

| Section | Check | Target | Worst measured | at | Pass |
|---|---|---|---|---|---|
| B. Null / latency | flat null residual ZL OS 1x | = 0.0000  | 0.0000  | 48 k mono | PASS 6/6 |
| B. Null / latency | impulse peak = reported latency ZL OS 1x | = 0.0000 samples | 0.0000 samples | 48 k mono | PASS 6/6 |
| B. Null / latency | flat broadband residual ZL OS 2x (OS filters band-limit; in-band null below) | (info) | -23.754 .. -7.630 dBFS | all | - |
| B. Null / latency | impulse peak = reported latency ZL OS 2x | = 0.0000 samples | 0.0000 samples | 48 k mono | PASS 6/6 |
| B. Null / latency | flat broadband residual ZL OS 4x (OS filters band-limit; in-band null below) | (info) | -23.753 .. -7.630 dBFS | all | - |
| B. Null / latency | impulse peak = reported latency ZL OS 4x | = 0.0000 samples | 0.0000 samples | 48 k mono | PASS 6/6 |
| B. Null / latency | flat null residual NP OS 1x | = 0.0000  | 0.0000  | 48 k mono | PASS 6/6 |
| B. Null / latency | impulse peak = reported latency NP OS 1x | = 0.0000 samples | 0.0000 samples | 48 k mono | PASS 6/6 |
| B. Null / latency | flat broadband residual NP OS 2x (OS filters band-limit; in-band null below) | (info) | -23.754 .. -7.630 dBFS | all | - |
| B. Null / latency | impulse peak = reported latency NP OS 2x | = 0.0000 samples | 0.0000 samples | 48 k mono | PASS 6/6 |
| B. Null / latency | flat broadband residual NP OS 4x (OS filters band-limit; in-band null below) | (info) | -23.753 .. -7.630 dBFS | all | - |
| B. Null / latency | impulse peak = reported latency NP OS 4x | = 0.0000 samples | 0.0000 samples | 48 k mono | PASS 6/6 |
| B. Null / latency | flat null residual LP OS 1x | = 0.0000  | 0.0000  | 48 k mono | PASS 6/6 |
| B. Null / latency | impulse peak = reported latency LP OS 1x | = 0.0000 samples | 0.0000 samples | 48 k mono | PASS 6/6 |
| B. Null / latency | flat broadband residual LP OS 2x (OS filters band-limit; in-band null below) | (info) | -23.266 .. -7.827 dBFS | all | - |
| B. Null / latency | impulse peak = reported latency LP OS 2x | = 0.0000 samples | 0.0000 samples | 48 k mono | PASS 6/6 |
| B. Null / latency | flat broadband residual LP OS 4x (OS filters band-limit; in-band null below) | (info) | -23.265 .. -7.827 dBFS | all | - |
| B. Null / latency | impulse peak = reported latency LP OS 4x | = 0.0000 samples | 0.0000 samples | 48 k mono | PASS 6/6 |
| B. Null / latency | OS 2x in-band null (1 kHz) | < -90.000 dBFS | -125.9 dBFS | 88.2 k stereo | PASS 6/6 |
| B. Null / latency | OS 4x in-band null (1 kHz) | < -90.000 dBFS | -123.7 dBFS | 88.2 k stereo | PASS 6/6 |
| B. Null / latency | Mix 0 % residual (dry, NP) | = 0.0000  | 0.0000  | 48 k mono | PASS 6/6 |
| C. Block-size invariance | max difference vs block 1024 (ZL) | = 0.0000  | 0.0000  | 48 k mono | PASS 6/6 |
| C. Block-size invariance | max difference vs block 1024 (NP) | = 0.0000  | 0.0000  | 48 k mono | PASS 6/6 |
| C. Block-size invariance | max difference vs block 1024 (LP) | = 0.0000  | 0.0000  | 48 k mono | PASS 6/6 |
| D. Fuzz / stress | random extreme automation: all samples finite | yes | 1.000  | 48 k mono | PASS 6/6 |
| D. Fuzz / stress | random extreme automation: peak output | < 48.000 dBFS | 10.034 dBFS | 44.1 k stereo | PASS 6/6 |
| D. Fuzz / stress | +24 dBFS noise, extreme boosts: finite | yes | 1.000  | 48 k mono | PASS 6/6 |
| D. Fuzz / stress | digital silence in -> out (fresh) | = 0.0000  | 0.0000  | 48 k mono | PASS 6/6 |
| D. Fuzz / stress | silence after loud noise, 3-4 s later | < -140.0 dBFS | -333.5 dBFS | 48 k mono | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: gain jump -12 <-> +12 dB | < -60.000 dBFS | -90.046 dBFS | 48 k mono | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: freq jump 300 Hz <-> 3 kHz | < -60.000 dBFS | -117.3 dBFS | 96 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: Q jump 0.3 <-> 10 | < -60.000 dBFS | -84.783 dBFS | 48 k mono | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: type bell <-> low shelf | < -60.000 dBFS | -110.2 dBFS | 192 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: band bypass toggle | < -60.000 dBFS | -132.4 dBFS | 192 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: band add/remove | < -60.000 dBFS | -132.4 dBFS | 192 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: placement stereo <-> mid | < -60.000 dBFS | -132.6 dBFS | 88.2 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: slope 12 <-> 48 (low cut) | < -60.000 dBFS | -105.5 dBFS | 192 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: module on/off (Passive Tube) | < -60.000 dBFS | -113.2 dBFS | 88.2 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: Passive Tube low freq 20 <-> 100 | < -60.000 dBFS | -143.7 dBFS | 192 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: Console E <-> G | < -60.000 dBFS | -74.958 dBFS | 192 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: phase mode ZL <-> NP | < -60.000 dBFS | -115.3 dBFS | 88.2 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: phase mode ZL <-> LP | < -60.000 dBFS | -115.1 dBFS | 192 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: phase mode NP <-> LP | < -60.000 dBFS | -115.0 dBFS | 192 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: oversampling 1x <-> 4x | < -60.000 dBFS | -115.3 dBFS | 88.2 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: output gain -12 <-> +6 | < -60.000 dBFS | -76.912 dBFS | 48 k mono | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: solo-free mix 0 <-> 100 % | < -60.000 dBFS | -85.110 dBFS | 48 k mono | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: high shelf Q 12 +12 dB: 14 kHz <-> 28 kHz | < -50.000 dBFS | -55.527 dBFS | 96 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: notch Q 2: 15 kHz <-> 30 kHz | < -60.000 dBFS | -90.455 dBFS | 44.1 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: bell 21 kHz Q 8: -18 <-> +18 dB | < -60.000 dBFS | -91.775 dBFS | 44.1 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: dynamic mode broadband <-> spectral (100 Hz band, acting) | < -60.000 dBFS | -64.605 dBFS | 44.1 k stereo | PASS 6/6 |
| E. Click-free automation and switching | HF burst > 6 kHz: spectral band on/off while acting | < -60.000 dBFS | -67.704 dBFS | 48 k mono | PASS 6/6 |
| F. Dynamic bands | static gain change: thr -30, range -12, ratio 20, in -20 dBFS | -10.000 .. -9.000 dB | -9.585 dB | 44.1 k stereo | PASS 6/6 |
| F. Dynamic bands | static gain change: thr -30, range -12, ratio 2, in -20 dBFS | -5.500 .. -4.500 dB | -5.045 dB | 44.1 k stereo | PASS 6/6 |
| F. Dynamic bands | static gain change: thr -30, range -4, ratio 20, in -10 dBFS (range limit) | -4.500 .. -3.500 dB | -4.000 dB | 192 k stereo | PASS 6/6 |
| F. Dynamic bands | static gain change: thr -30, range +6, ratio 4, in -20 dBFS (upward) | 5.500 .. 6.500 dB | 6.000 dB | 192 k stereo | PASS 6/6 |
| F. Dynamic bands | static gain change: below threshold: no change | -0.5000 .. 0.5000 dB | -2.04e-07 dB | 192 k stereo | PASS 6/6 |
| F. Dynamic bands | attack 30 ms: 63 % time | 24.000 .. 38.000 ms | 30.249 ms | 88.2 k stereo | PASS 6/6 |
| F. Dynamic bands | release 150 ms: 63 % time | 120.0 .. 180.0 ms | 154.8 ms | 192 k stereo | PASS 6/6 |
| F. Dynamic bands | attack 10 ms: 63 % time | 8.000 .. 14.000 ms | 10.250 ms | 192 k stereo | PASS 6/6 |
| F. Dynamic bands | release 400 ms: 63 % time | 320.0 .. 480.0 ms | 404.8 ms | 192 k stereo | PASS 6/6 |
| F. Dynamic bands | external SC quiet: no change | -0.3000 .. 0.3000 dB | 0.0013 dB | 192 k stereo | PASS 6/6 |
| F. Dynamic bands | external SC loud: ducks by range | -9.500 .. -8.500 dB | -8.999 dB | 192 k stereo | PASS 6/6 |
| F. Dynamic bands | source = Band 2 region: 3 kHz band reacts to 9 kHz | -6.500 .. -5.500 dB | -6.000 dB | 48 k mono | PASS 6/6 |
| F. Dynamic bands | spectral: loud tone in the band reduced by the range | -12.500 .. -11.500 dB | -11.828 dB | 192 k stereo | PASS 6/6 |
| F. Dynamic bands | spectral: quiet tone in the same band untouched | -0.1000 .. 0.1000 dB | -0.000629 dB | 48 k mono | PASS 6/6 |
| F. Dynamic bands | spectral: loud tone outside the band (300 Hz) ~ untouched | -0.5000 .. 0.0500 dB | -0.1638 dB | 88.2 k stereo | PASS 6/6 |
| F. Dynamic bands | broadband for comparison pulls the quiet tone down (dB) | < -6.000 dB | -8.294 dB | 192 k stereo | PASS 6/6 |
| F. Dynamic bands | spectral upward (range +6): loud tone raised | 5.500 .. 6.500 dB | 5.958 dB | 48 k mono | PASS 6/6 |
| F. Dynamic bands | spectral upward: quiet tone untouched | -0.1000 .. 0.1000 dB | 0.000667 dB | 88.2 k stereo | PASS 6/6 |
| F. Dynamic bands | spectral below threshold: output identical to the static band | < 1e-09  | 0.0000  | 48 k mono | PASS 6/6 |
| F. Dynamic bands | spectral side-chain: the side-chain's frequency cut in the programme | -9.500 .. -8.500 dB | -8.942 dB | 192 k stereo | PASS 6/6 |
| F. Dynamic bands | spectral side-chain: other frequencies untouched | -0.1000 .. 0.1000 dB | 0.000502 dB | 44.1 k stereo | PASS 6/6 |
| G. Stereo placement | mono: Side band inactive (null) | = 0.0000  | 0.0000  | 48 k mono | PASS 1/1 |
| L. Independent references (textbook formulas, not the engine's model) | Butterworth cuts, all slopes, vs exact curve (>= -24 dB; -48 dB at 0.5 dB, scaled) (ZL) | < 0.1000 dB | 0.0123 dB | 44.1 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | Butterworth corner = -3.01 dB, 12..96 dB/oct (ZL) | < 0.0200 dB | 0.0014 dB | 88.2 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | Butterworth cuts, all slopes, vs exact curve (>= -24 dB; -48 dB at 0.5 dB, scaled) (NP) | < 0.1000 dB | 0.0219 dB | 48 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | Butterworth corner = -3.01 dB, 12..96 dB/oct (NP) | < 0.0200 dB | 7.61e-06 dB | 48 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | shelves/tilt Q 0.1..5 vs RBJ analog | < 0.1000 dB | 0.0054 dB | 44.1 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | shelves/tilt Q 5..24 vs RBJ analog (1x) | < 0.1000 dB | 0.0563 dB | 192 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | every type centred 12..30 kHz vs analog (1x, ZL + NP) | < 0.1000 dB | 0.0064 dB | 44.1 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | Natural Phase band-pass 96 dB/oct Q 12 vs analog magnitude | < 0.1000 dB | 0.0199 dB | 44.1 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | Natural Phase band-pass 96 dB/oct Q 12 vs analog phase (>= -20 dB) | < 3.000 deg | 1.107 deg | 44.1 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | dynamic high shelf 19.5 kHz Q 12 (tracked fit): output finite | = 1.000  | 1.000  | 88.2 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | dynamic high shelf 19.5 kHz Q 12 (tracked fit): output peak | < 23.979 dBFS | 20.813 dBFS | 48 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | band-pass 12..96 dB/oct vs n cascaded 2nd-order sections | < 0.1000 dB | 0.0116 dB | 44.1 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | band-pass: each steeper slope attenuates more (1 oct out) | = 1.000  | 1.000  | 88.2 k stereo | PASS 5/5 |
| L. Independent references (textbook formulas, not the engine's model) | input/output gain exact after smoothing (relative error) | < 2e-07  | 8.85e-08  | 96 k stereo | PASS 5/5 |
| F. Dynamic bands | spectral on Side: Side tone cut by the range | -12.500 .. -11.500 dB | -11.828 dB | 192 k stereo | PASS 5/5 |
| F. Dynamic bands | spectral on Side: Mid untouched | -0.1000 .. 0.1000 dB | 1.91e-07 dB | 48 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Left bands (ZL) | < -100.0 dBFS | -600.0 dBFS | 88.2 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Left (ZL) | > -40.000 dBFS | 2.513 dBFS | 192 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Right bands (ZL) | < -100.0 dBFS | -600.0 dBFS | 88.2 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Right (ZL) | > -40.000 dBFS | -2.308 dBFS | 44.1 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Mid bands (ZL) | < -100.0 dBFS | -141.0 dBFS | 48 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Mid (ZL) | > -40.000 dBFS | 1.213 dBFS | 44.1 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Side bands (ZL) | < -100.0 dBFS | -142.6 dBFS | 88.2 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Side (ZL) | > -40.000 dBFS | 0.2828 dBFS | 44.1 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Left bands (NP) | < -100.0 dBFS | -600.0 dBFS | 88.2 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Left (NP) | > -40.000 dBFS | 2.497 dBFS | 192 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Right bands (NP) | < -100.0 dBFS | -600.0 dBFS | 88.2 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Right (NP) | > -40.000 dBFS | -2.065 dBFS | 44.1 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Mid bands (NP) | < -100.0 dBFS | -122.1 dBFS | 192 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Mid (NP) | > -40.000 dBFS | 0.6796 dBFS | 48 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Side bands (NP) | < -100.0 dBFS | -123.9 dBFS | 192 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Side (NP) | > -40.000 dBFS | 0.5423 dBFS | 44.1 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Left bands (LP) | < -100.0 dBFS | -600.0 dBFS | 88.2 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Left (LP) | > -40.000 dBFS | 1.060 dBFS | 192 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Right bands (LP) | < -100.0 dBFS | -600.0 dBFS | 88.2 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Right (LP) | > -40.000 dBFS | -5.282 dBFS | 44.1 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Mid bands (LP) | < -100.0 dBFS | -129.5 dBFS | 192 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Mid (LP) | > -40.000 dBFS | -0.8734 dBFS | 48 k stereo | PASS 5/5 |
| G. Stereo placement | untouched component, Side bands (LP) | < -100.0 dBFS | -129.9 dBFS | 192 k stereo | PASS 5/5 |
| G. Stereo placement | processed component changes, Side (LP) | > -40.000 dBFS | -0.5534 dBFS | 48 k stereo | PASS 5/5 |
| H. Character modules | Passive Tube 60 Hz boost 10 alone: level at 30 Hz | 11.000 .. 14.000 dB | 13.554 dB | 88.2 k stereo | PASS 5/5 |
| H. Character modules | boost 10 + atten 5: LF bump at 30 Hz | 3.000 .. 8.000 dB | 5.008 dB | 192 k stereo | PASS 5/5 |
| H. Character modules | boost 10 + atten 5: dip depth | -6.000 .. -2.000 dB | -3.459 dB | 88.2 k stereo | PASS 5/5 |
| H. Character modules | boost 10 + atten 5: dip position / selection | 3.000 .. 8.000 x | 3.770 x | 88.2 k stereo | PASS 5/5 |
| H. Character modules | boost 10 + atten 10: dip up to ~8 dB | -9.500 .. -6.000 dB | -7.840 dB | 192 k stereo | PASS 5/5 |
| H. Character modules | dip deepens with atten | yes | 1.000  | 88.2 k stereo | PASS 5/5 |
| H. Character modules | module curve vs model at drive 0: ptOn | < 0.1000 dB | 0.0054 dB | 44.1 k stereo | PASS 5/5 |
| H. Character modules | module curve vs model at drive 0: indOn | < 0.1000 dB | 0.0021 dB | 96 k stereo | PASS 5/5 |
| H. Character modules | module curve vs model at drive 0: conOn | < 0.1000 dB | 0.0061 dB | 48 k stereo | PASS 5/5 |
| H. Character modules | module curve vs model at drive 0: airOn | < 0.1000 dB | 0.0016 dB | 44.1 k stereo | PASS 5/5 |
| H. Character modules | Passive Tube THD, drive 100 %, -6 dBFS 1 kHz | 0.3000 .. 8.000 % | 5.245 % | 88.2 k stereo | PASS 5/5 |
| H. Character modules | Passive Tube: 3rd harmonic dominates (H3/H2) | > 1.500 x | 1.825 x | 192 k stereo | PASS 5/5 |
| H. Character modules | Inductor: 50 Hz distorts more than 2 kHz | > 2.000 x | 22.434 x | 192 k stereo | PASS 5/5 |
| H. Character modules | Console THD, drive 100 %, -6 dBFS | 0.1000 .. 6.000 % | 1.606 % | 44.1 k stereo | PASS 5/5 |
| H. Character modules | drive 0: harmonics | < -120.0 dB | -144.9 dB | 192 k stereo | PASS 5/5 |
| I. Solo / auto gain / mono | solo band 1 kHz: tone in band passes | -1.000 .. 1.000 dB | -4.82e-07 dB | 88.2 k stereo | PASS 5/5 |
| I. Solo / auto gain / mono | solo band 1 kHz: 100 Hz tone rejected | < -20.000 dB | -25.946 dB | 192 k stereo | PASS 5/5 |
| I. Solo / auto gain / mono | auto gain loudness match (type 0, +6 dB) | -1.500 .. 1.500 dB | -0.2134 dB | 44.1 k stereo | PASS 5/5 |
| I. Solo / auto gain / mono | auto gain loudness match (type 2, +8 dB) | -1.500 .. 1.500 dB | -0.3797 dB | 192 k stereo | PASS 5/5 |
| I. Solo / auto gain / mono | auto gain loudness match (type 1, -6 dB) | -1.500 .. 1.500 dB | -0.2969 dB | 192 k stereo | PASS 5/5 |
| A. Clean magnitude vs analog (ZL) | worst error, target >= -24 dB (592 cases) | < 0.1001 dB | 0.0347 dB | 44.1 k stereo | PASS 3/3 |
| A. Clean magnitude vs analog (ZL) | worst error, cuts -24..-48 dB | < 0.5000 dB | 0.0605 dB | 44.1 k stereo | PASS 3/3 |
| A. Clean magnitude vs analog (ZL) | cases outside tolerance | = 0.0000  | 0.0000  | 44.1 k stereo | PASS 3/3 |
| A. Clean magnitude vs analog (NP) | worst error, target >= -24 dB (592 cases) | < 0.1001 dB | 0.0624 dB | 44.1 k stereo | PASS 3/3 |
| A. Clean magnitude vs analog (NP) | worst error, cuts -24..-48 dB | < 0.5000 dB | 0.0191 dB | 44.1 k stereo | PASS 3/3 |
| A. Clean magnitude vs analog (NP) | cases outside tolerance | = 0.0000  | 0.0000  | 44.1 k stereo | PASS 3/3 |
| A. Clean magnitude vs analog (LP) | worst error, target >= -24 dB (493 cases) | < 0.1001 dB | 0.0989 dB | 44.1 k stereo | PASS 3/3 |
| A. Clean magnitude vs analog (LP) | worst error, cuts -24..-48 dB | < 0.5000 dB | 0.0680 dB | 96 k stereo | PASS 3/3 |
| A. Clean magnitude vs analog (LP) | cases outside tolerance | = 0.0000  | 0.0000  | 44.1 k stereo | PASS 3/3 |
| A. Clean magnitude vs analog (LP, High resolution, low bands) | worst error, target >= -24 dB (19 cases) | < 0.1001 dB | 0.0268 dB | 44.1 k stereo | PASS 3/3 |
| A. Clean magnitude vs analog (LP, High resolution, low bands) | worst error, cuts -24..-48 dB | < 0.5000 dB | 0.000274 dB | 48 k stereo | PASS 3/3 |
| A. Clean magnitude vs analog (LP, High resolution, low bands) | cases outside tolerance | = 0.0000  | 0.0000  | 44.1 k stereo | PASS 3/3 |
| P. Factory presets | number of factory presets | > 29.500  | 42.000  | 48 k stereo | PASS 1/1 |
| P. Factory presets | every preset value refers to a real parameter id | yes | 1.000  | 48 k stereo | PASS 1/1 |
| P. Factory presets | names/descriptions: no protected names, description present | yes | 1.000  | 48 k stereo | PASS 1/1 |
| P. Factory presets | Vocals presets | > 11.500  | 14.000  | 48 k stereo | PASS 1/1 |
| P. Factory presets | Beats & Instruments presets | > 11.500  | 13.000  | 48 k stereo | PASS 1/1 |
| P. Factory presets | Mix Bus & Master presets | > 7.500  | 10.000  | 48 k stereo | PASS 1/1 |
| P. Factory presets | Axell Signature presets | > 2.500  | 4.000  | 48 k stereo | PASS 1/1 |
| P. Factory presets | all presets: output finite on pink noise | yes | 1.000  | 48 k stereo | PASS 1/1 |
| P. Factory presets | loudness vs bypass, pink noise -18 dBFS, K-weighted (worst: Master - Air & Clarity) | -0.1000 .. 0.1000 LU | 0.0484 LU | 48 k stereo | PASS 1/1 |
| K. Real-time safety | heap allocations inside setParams()+process() (all modes/OS, automation, live mode switch) | = 0.0000  | 0.0000  | 48 k stereo | PASS 1/1 |
| J. CPU (48 kHz, 128-sample blocks, stereo) | vocal: 7 bands (2 dynamic) + HP, Zero Latency | (info) | 1.456 % of one core | all | - |
| J. CPU (48 kHz, 128-sample blocks, stereo) | beat bus: 6 bands + Console + Passive Tube, OS 2x | (info) | 2.974 % of one core | all | - |
| J. CPU (48 kHz, 128-sample blocks, stereo) | master: 8 bands (M/S), Natural Phase + Air | (info) | 1.612 % of one core | all | - |
| J. CPU (48 kHz, 128-sample blocks, stereo) | master: same in Linear Phase (Medium) | (info) | 1.329 % of one core | all | - |
| J. CPU (48 kHz, 128-sample blocks, stereo) | vocal + 2 spectral bands (harshness 3 kHz, sibilance 7 kHz) | (info) | 2.761 % of one core | all | - |
| J. CPU (48 kHz, 128-sample blocks, stereo) | near Nyquist: 4 fitted bands at 48 kHz (2 dynamic, gain moving) | (info) | 4.340 % of one core | all | - |
| J. CPU (48 kHz, 128-sample blocks, stereo) | worst case: 12 dynamic Character bands + all modules, OS 4x | (info) | 13.320 % of one core | all | - |

## Detail per configuration

| Check | 192 k stereo | 44.1 k stereo | 48 k mono | 48 k stereo | 88.2 k stereo | 96 k stereo |
|---|---|---|---|---|---|---|
| B. Null / latency: flat null residual ZL OS 1x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: impulse peak = reported latency ZL OS 1x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: flat broadband residual ZL OS 2x (OS filters band-limit; in-band null below) | -7.630 | -23.754 | -23.754 | -23.754 | -7.630 | -7.630 |
| B. Null / latency: impulse peak = reported latency ZL OS 2x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: flat broadband residual ZL OS 4x (OS filters band-limit; in-band null below) | -7.630 | -23.753 | -23.753 | -23.753 | -7.630 | -7.630 |
| B. Null / latency: impulse peak = reported latency ZL OS 4x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: flat null residual NP OS 1x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: impulse peak = reported latency NP OS 1x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: flat broadband residual NP OS 2x (OS filters band-limit; in-band null below) | -7.630 | -23.754 | -23.754 | -23.754 | -7.630 | -7.630 |
| B. Null / latency: impulse peak = reported latency NP OS 2x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: flat broadband residual NP OS 4x (OS filters band-limit; in-band null below) | -7.630 | -23.753 | -23.753 | -23.753 | -7.630 | -7.630 |
| B. Null / latency: impulse peak = reported latency NP OS 4x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: flat null residual LP OS 1x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: impulse peak = reported latency LP OS 1x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: flat broadband residual LP OS 2x (OS filters band-limit; in-band null below) | -7.827 | -22.531 | -23.266 | -22.531 | -8.132 | -8.132 |
| B. Null / latency: impulse peak = reported latency LP OS 2x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: flat broadband residual LP OS 4x (OS filters band-limit; in-band null below) | -7.827 | -22.531 | -23.265 | -22.531 | -8.132 | -8.132 |
| B. Null / latency: impulse peak = reported latency LP OS 4x | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| B. Null / latency: OS 2x in-band null (1 kHz) | -136.5 | -130.5 | -128.2 | -128.2 | -125.9 | -127.6 |
| B. Null / latency: OS 4x in-band null (1 kHz) | -136.5 | -127.6 | -138.5 | -138.5 | -123.7 | -125.9 |
| B. Null / latency: Mix 0 % residual (dry, NP) | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| C. Block-size invariance: max difference vs block 1024 (ZL) | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| C. Block-size invariance: max difference vs block 1024 (NP) | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| C. Block-size invariance: max difference vs block 1024 (LP) | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| D. Fuzz / stress: random extreme automation: all samples finite | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 |
| D. Fuzz / stress: random extreme automation: peak output | 4.820 | 10.034 | 6.652 | 9.611 | 6.819 | 6.437 |
| D. Fuzz / stress: +24 dBFS noise, extreme boosts: finite | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 |
| D. Fuzz / stress: digital silence in -> out (fresh) | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| D. Fuzz / stress: silence after loud noise, 3-4 s later | -342.6 | -335.7 | -333.5 | -333.5 | -334.2 | -334.5 |
| E. Click-free automation and switching: HF burst > 6 kHz: gain jump -12 <-> +12 dB | -91.497 | -91.245 | -90.046 | -90.046 | -91.329 | -91.492 |
| E. Click-free automation and switching: HF burst > 6 kHz: freq jump 300 Hz <-> 3 kHz | -118.1 | -120.6 | -117.9 | -117.9 | -117.6 | -117.3 |
| E. Click-free automation and switching: HF burst > 6 kHz: Q jump 0.3 <-> 10 | -87.059 | -84.882 | -84.783 | -84.783 | -84.890 | -85.249 |
| E. Click-free automation and switching: HF burst > 6 kHz: type bell <-> low shelf | -110.2 | -111.2 | -111.1 | -111.1 | -110.4 | -110.5 |
| E. Click-free automation and switching: HF burst > 6 kHz: band bypass toggle | -132.4 | -133.7 | -133.6 | -133.6 | -133.2 | -133.1 |
| E. Click-free automation and switching: HF burst > 6 kHz: band add/remove | -132.4 | -133.7 | -133.6 | -133.6 | -133.2 | -133.1 |
| E. Click-free automation and switching: HF burst > 6 kHz: placement stereo <-> mid | -132.8 | -133.4 | -133.7 | -133.7 | -132.6 | -133.1 |
| E. Click-free automation and switching: HF burst > 6 kHz: slope 12 <-> 48 (low cut) | -105.5 | -106.2 | -105.9 | -105.9 | -105.6 | -105.5 |
| E. Click-free automation and switching: HF burst > 6 kHz: module on/off (Passive Tube) | -113.5 | -114.7 | -114.1 | -114.1 | -113.2 | -113.8 |
| E. Click-free automation and switching: HF burst > 6 kHz: Passive Tube low freq 20 <-> 100 | -143.7 | -145.9 | -145.4 | -145.4 | -144.7 | -144.5 |
| E. Click-free automation and switching: HF burst > 6 kHz: Console E <-> G | -74.958 | -88.887 | -87.727 | -87.727 | -81.298 | -81.865 |
| E. Click-free automation and switching: HF burst > 6 kHz: phase mode ZL <-> NP | -115.5 | -115.9 | -116.0 | -116.0 | -115.3 | -115.4 |
| E. Click-free automation and switching: HF burst > 6 kHz: phase mode ZL <-> LP | -115.1 | -115.8 | -115.8 | -115.8 | -115.3 | -115.2 |
| E. Click-free automation and switching: HF burst > 6 kHz: phase mode NP <-> LP | -115.0 | -115.8 | -115.8 | -115.8 | -115.4 | -115.1 |
| E. Click-free automation and switching: HF burst > 6 kHz: oversampling 1x <-> 4x | -115.4 | -116.0 | -116.0 | -116.0 | -115.3 | -115.5 |
| E. Click-free automation and switching: HF burst > 6 kHz: output gain -12 <-> +6 | -78.045 | -77.017 | -76.912 | -76.912 | -77.525 | -77.969 |
| E. Click-free automation and switching: HF burst > 6 kHz: solo-free mix 0 <-> 100 % | -86.264 | -85.305 | -85.110 | -85.110 | -85.242 | -86.169 |
| E. Click-free automation and switching: HF burst > 6 kHz: high shelf Q 12 +12 dB: 14 kHz <-> 28 kHz | -112.3 | -92.002 | -99.164 | -99.164 | -55.933 | -55.527 |
| E. Click-free automation and switching: HF burst > 6 kHz: notch Q 2: 15 kHz <-> 30 kHz | -132.5 | -90.455 | -91.234 | -91.234 | -132.0 | -131.9 |
| E. Click-free automation and switching: HF burst > 6 kHz: bell 21 kHz Q 8: -18 <-> +18 dB | -130.7 | -91.775 | -125.0 | -125.0 | -128.9 | -128.7 |
| E. Click-free automation and switching: HF burst > 6 kHz: dynamic mode broadband <-> spectral (100 Hz band, acting) | -78.997 | -64.605 | -66.903 | -66.903 | -73.724 | -74.776 |
| E. Click-free automation and switching: HF burst > 6 kHz: spectral band on/off while acting | -68.794 | -68.302 | -67.704 | -67.704 | -69.149 | -69.087 |
| F. Dynamic bands: static gain change: thr -30, range -12, ratio 20, in -20 dBFS | -9.581 | -9.585 | -9.571 | -9.571 | -9.584 | -9.578 |
| F. Dynamic bands: static gain change: thr -30, range -12, ratio 2, in -20 dBFS | -5.043 | -5.045 | -5.037 | -5.037 | -5.044 | -5.041 |
| F. Dynamic bands: static gain change: thr -30, range -4, ratio 20, in -10 dBFS (range limit) | -4.000 | -4.000 | -4.000 | -4.000 | -4.000 | -4.000 |
| F. Dynamic bands: static gain change: thr -30, range +6, ratio 4, in -20 dBFS (upward) | 6.000 | 6.000 | 6.000 | 6.000 | 6.000 | 6.000 |
| F. Dynamic bands: static gain change: below threshold: no change | -2.04e-07 | -1.32e-08 | -1.04e-07 | -1.04e-07 | -7.73e-09 | -2.17e-08 |
| F. Dynamic bands: attack 30 ms: 63 % time | 30.250 | 30.431 | 30.333 | 30.333 | 30.249 | 30.333 |
| F. Dynamic bands: release 150 ms: 63 % time | 154.8 | 154.7 | 154.7 | 154.7 | 154.7 | 154.8 |
| F. Dynamic bands: attack 10 ms: 63 % time | 10.250 | 10.476 | 10.333 | 10.333 | 10.295 | 10.333 |
| F. Dynamic bands: release 400 ms: 63 % time | 404.8 | 404.7 | 404.7 | 404.7 | 404.7 | 404.7 |
| F. Dynamic bands: external SC quiet: no change | 0.0013 | -7.48e-06 | -5.29e-06 | -5.29e-06 | 0.000121 | 2.15e-05 |
| F. Dynamic bands: external SC loud: ducks by range | -8.999 | -9.000 | -9.000 | -9.000 | -9.000 | -9.000 |
| F. Dynamic bands: source = Band 2 region: 3 kHz band reacts to 9 kHz | -6.000 | -6.000 | -6.000 | -6.000 | -6.000 | -6.000 |
| F. Dynamic bands: spectral: loud tone in the band reduced by the range | -11.828 | -11.935 | -11.830 | -11.830 | -11.936 | -11.828 |
| F. Dynamic bands: spectral: quiet tone in the same band untouched | -0.000563 | 0.000628 | -0.000629 | -0.000629 | 0.000627 | -0.000589 |
| F. Dynamic bands: spectral: loud tone outside the band (300 Hz) ~ untouched | -0.1647 | -0.1638 | -0.1647 | -0.1647 | -0.1638 | -0.1647 |
| F. Dynamic bands: broadband for comparison pulls the quiet tone down (dB) | -8.294 | -8.294 | -8.294 | -8.294 | -8.294 | -8.294 |
| F. Dynamic bands: spectral upward (range +6): loud tone raised | 5.959 | 6.015 | 5.958 | 5.958 | 6.015 | 5.959 |
| F. Dynamic bands: spectral upward: quiet tone untouched | -0.000462 | 0.000664 | -0.000489 | -0.000489 | 0.000667 | -0.000482 |
| F. Dynamic bands: spectral below threshold: output identical to the static band | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| F. Dynamic bands: spectral side-chain: the side-chain's frequency cut in the programme | -8.942 | -8.958 | -8.944 | -8.944 | -8.958 | -8.943 |
| F. Dynamic bands: spectral side-chain: other frequencies untouched | -0.000209 | 0.000502 | -0.000251 | -0.000251 | 0.000491 | -0.000223 |
| G. Stereo placement: mono: Side band inactive (null) | - | - | 0.0000 | - | - | - |
| L. Independent references (textbook formulas, not the engine's model): Butterworth cuts, all slopes, vs exact curve (>= -24 dB; -48 dB at 0.5 dB, scaled) (ZL) | 0.0010 | 0.0123 | - | 0.0108 | 0.0033 | 0.0029 |
| L. Independent references (textbook formulas, not the engine's model): Butterworth corner = -3.01 dB, 12..96 dB/oct (ZL) | 0.000304 | 3.07e-06 | - | 2.86e-06 | 0.0014 | 0.0012 |
| L. Independent references (textbook formulas, not the engine's model): Butterworth cuts, all slopes, vs exact curve (>= -24 dB; -48 dB at 0.5 dB, scaled) (NP) | 0.0014 | 0.0211 | - | 0.0219 | 0.0053 | 0.0055 |
| L. Independent references (textbook formulas, not the engine's model): Butterworth corner = -3.01 dB, 12..96 dB/oct (NP) | 3.86e-06 | 7.46e-06 | - | 7.61e-06 | 4.16e-06 | 1.63e-06 |
| L. Independent references (textbook formulas, not the engine's model): shelves/tilt Q 0.1..5 vs RBJ analog | 0.000938 | 0.0054 | - | 0.0018 | 0.0044 | 0.0019 |
| L. Independent references (textbook formulas, not the engine's model): shelves/tilt Q 5..24 vs RBJ analog (1x) | 0.0563 | 0.0061 | - | 0.0343 | 0.0019 | 0.0013 |
| L. Independent references (textbook formulas, not the engine's model): every type centred 12..30 kHz vs analog (1x, ZL + NP) | 0.0020 | 0.0064 | - | 0.0062 | 0.0018 | 0.0032 |
| L. Independent references (textbook formulas, not the engine's model): Natural Phase band-pass 96 dB/oct Q 12 vs analog magnitude | 0.0011 | 0.0199 | - | 0.0169 | 0.0050 | 0.0043 |
| L. Independent references (textbook formulas, not the engine's model): Natural Phase band-pass 96 dB/oct Q 12 vs analog phase (>= -20 dB) | 0.2477 | 1.107 | - | 0.9787 | 0.5592 | 0.4939 |
| L. Independent references (textbook formulas, not the engine's model): dynamic high shelf 19.5 kHz Q 12 (tracked fit): output finite | 1.000 | 1.000 | - | 1.000 | 1.000 | 1.000 |
| L. Independent references (textbook formulas, not the engine's model): dynamic high shelf 19.5 kHz Q 12 (tracked fit): output peak | 10.965 | 11.212 | - | 20.813 | 12.024 | 11.954 |
| L. Independent references (textbook formulas, not the engine's model): band-pass 12..96 dB/oct vs n cascaded 2nd-order sections | 9.69e-05 | 0.0116 | - | 0.0062 | 0.000674 | 0.000296 |
| L. Independent references (textbook formulas, not the engine's model): band-pass: each steeper slope attenuates more (1 oct out) | 1.000 | 1.000 | - | 1.000 | 1.000 | 1.000 |
| L. Independent references (textbook formulas, not the engine's model): input/output gain exact after smoothing (relative error) | 8.84e-08 | 8.7e-08 | - | 8.73e-08 | 8.81e-08 | 8.85e-08 |
| F. Dynamic bands: spectral on Side: Side tone cut by the range | -11.828 | -11.935 | - | -11.830 | -11.936 | -11.828 |
| F. Dynamic bands: spectral on Side: Mid untouched | 1.14e-07 | -6.8e-09 | - | 1.91e-07 | 1.24e-08 | 6.41e-08 |
| G. Stereo placement: untouched component, Left bands (ZL) | -600.0 | -600.0 | - | -600.0 | -600.0 | -600.0 |
| G. Stereo placement: processed component changes, Left (ZL) | 2.513 | 4.752 | - | 4.812 | 4.291 | 4.107 |
| G. Stereo placement: untouched component, Right bands (ZL) | -600.0 | -600.0 | - | -600.0 | -600.0 | -600.0 |
| G. Stereo placement: processed component changes, Right (ZL) | 3.468 | -2.308 | - | -2.107 | 1.408 | 2.244 |
| G. Stereo placement: untouched component, Mid bands (ZL) | -142.6 | -144.5 | - | -141.0 | -142.6 | -142.6 |
| G. Stereo placement: processed component changes, Mid (ZL) | 2.102 | 1.213 | - | 1.600 | 1.992 | 2.590 |
| G. Stereo placement: untouched component, Side bands (ZL) | -142.9 | -144.5 | - | -142.6 | -142.6 | -142.6 |
| G. Stereo placement: processed component changes, Side (ZL) | 1.814 | 0.2828 | - | 0.5944 | 1.375 | 1.859 |
| G. Stereo placement: untouched component, Left bands (NP) | -600.0 | -600.0 | - | -600.0 | -600.0 | -600.0 |
| G. Stereo placement: processed component changes, Left (NP) | 2.497 | 4.751 | - | 4.796 | 4.274 | 4.081 |
| G. Stereo placement: untouched component, Right bands (NP) | -600.0 | -600.0 | - | -600.0 | -600.0 | -600.0 |
| G. Stereo placement: processed component changes, Right (NP) | 3.606 | -2.065 | - | -1.936 | 1.344 | 2.177 |
| G. Stereo placement: untouched component, Mid bands (NP) | -122.1 | -130.1 | - | -129.7 | -124.7 | -126.7 |
| G. Stereo placement: processed component changes, Mid (NP) | 1.978 | 0.8002 | - | 0.6796 | 1.354 | 1.756 |
| G. Stereo placement: untouched component, Side bands (NP) | -123.9 | -129.3 | - | -129.7 | -127.6 | -126.7 |
| G. Stereo placement: processed component changes, Side (NP) | 2.478 | 0.5423 | - | 0.5653 | 1.488 | 1.581 |
| G. Stereo placement: untouched component, Left bands (LP) | -600.0 | -600.0 | - | -600.0 | -600.0 | -600.0 |
| G. Stereo placement: processed component changes, Left (LP) | 1.060 | 4.003 | - | 3.903 | 2.526 | 2.546 |
| G. Stereo placement: untouched component, Right bands (LP) | -600.0 | -600.0 | - | -600.0 | -600.0 | -600.0 |
| G. Stereo placement: processed component changes, Right (LP) | 3.310 | -5.282 | - | -5.243 | -1.211 | 0.1033 |
| G. Stereo placement: untouched component, Mid bands (LP) | -129.5 | -130.5 | - | -133.0 | -130.1 | -129.8 |
| G. Stereo placement: processed component changes, Mid (LP) | 1.902 | -0.7336 | - | -0.8734 | -0.4570 | 0.0272 |
| G. Stereo placement: untouched component, Side bands (LP) | -129.9 | -130.1 | - | -132.5 | -130.3 | -130.5 |
| G. Stereo placement: processed component changes, Side (LP) | 1.215 | -0.5144 | - | -0.5534 | 0.3616 | 0.8908 |
| H. Character modules: Passive Tube 60 Hz boost 10 alone: level at 30 Hz | 13.554 | 13.554 | - | 13.554 | 13.554 | 13.554 |
| H. Character modules: boost 10 + atten 5: LF bump at 30 Hz | 5.008 | 5.008 | - | 5.008 | 5.008 | 5.008 |
| H. Character modules: boost 10 + atten 5: dip depth | -3.459 | -3.459 | - | -3.459 | -3.459 | -3.459 |
| H. Character modules: boost 10 + atten 5: dip position / selection | 3.770 | 3.770 | - | 3.770 | 3.770 | 3.770 |
| H. Character modules: boost 10 + atten 10: dip up to ~8 dB | -7.840 | -7.840 | - | -7.840 | -7.840 | -7.840 |
| H. Character modules: dip deepens with atten | 1.000 | 1.000 | - | 1.000 | 1.000 | 1.000 |
| H. Character modules: module curve vs model at drive 0: ptOn | 0.000134 | 0.0054 | - | 0.0026 | 0.000111 | 0.000137 |
| H. Character modules: module curve vs model at drive 0: indOn | 0.0021 | 0.0019 | - | 0.0021 | 0.0019 | 0.0021 |
| H. Character modules: module curve vs model at drive 0: conOn | 0.0023 | 0.0019 | - | 0.0061 | 0.0031 | 0.0031 |
| H. Character modules: module curve vs model at drive 0: airOn | 5.37e-06 | 0.0016 | - | 0.000801 | 4.23e-05 | 1.66e-05 |
| H. Character modules: Passive Tube THD, drive 100 %, -6 dBFS 1 kHz | 5.245 | 5.245 | - | 5.245 | 5.245 | 5.245 |
| H. Character modules: Passive Tube: 3rd harmonic dominates (H3/H2) | 1.825 | 1.825 | - | 1.825 | 1.825 | 1.825 |
| H. Character modules: Inductor: 50 Hz distorts more than 2 kHz | 22.434 | 22.450 | - | 22.457 | 22.476 | 22.446 |
| H. Character modules: Console THD, drive 100 %, -6 dBFS | 1.606 | 1.606 | - | 1.606 | 1.606 | 1.606 |
| H. Character modules: drive 0: harmonics | -144.9 | -174.9 | - | -166.1 | -165.3 | -160.1 |
| I. Solo / auto gain / mono: solo band 1 kHz: tone in band passes | -2.44e-07 | -2.56e-08 | - | 3.05e-08 | -4.82e-07 | -3.46e-07 |
| I. Solo / auto gain / mono: solo band 1 kHz: 100 Hz tone rejected | -25.946 | -25.959 | - | -25.957 | -25.948 | -25.948 |
| I. Solo / auto gain / mono: auto gain loudness match (type 0, +6 dB) | -0.2123 | -0.2134 | - | -0.2133 | -0.2125 | -0.2125 |
| I. Solo / auto gain / mono: auto gain loudness match (type 2, +8 dB) | -0.3797 | -0.3762 | - | -0.3767 | -0.3783 | -0.3786 |
| I. Solo / auto gain / mono: auto gain loudness match (type 1, -6 dB) | -0.2969 | -0.2954 | - | -0.2956 | -0.2966 | -0.2966 |
| A. Clean magnitude vs analog (ZL): worst error, target >= -24 dB (592 cases) | - | 0.0347 | - | 0.0132 | - | 0.0026 |
| A. Clean magnitude vs analog (ZL): worst error, cuts -24..-48 dB | - | 0.0605 | - | 0.0533 | - | 0.0293 |
| A. Clean magnitude vs analog (ZL): cases outside tolerance | - | 0.0000 | - | 0.0000 | - | 0.0000 |
| A. Clean magnitude vs analog (NP): worst error, target >= -24 dB (592 cases) | - | 0.0624 | - | 0.0296 | - | 0.0055 |
| A. Clean magnitude vs analog (NP): worst error, cuts -24..-48 dB | - | 0.0191 | - | 0.0104 | - | 0.0026 |
| A. Clean magnitude vs analog (NP): cases outside tolerance | - | 0.0000 | - | 0.0000 | - | 0.0000 |
| A. Clean magnitude vs analog (LP): worst error, target >= -24 dB (493 cases) | - | 0.0989 | - | 0.0812 | - | 0.0812 |
| A. Clean magnitude vs analog (LP): worst error, cuts -24..-48 dB | - | 0.0660 | - | 0.0679 | - | 0.0680 |
| A. Clean magnitude vs analog (LP): cases outside tolerance | - | 0.0000 | - | 0.0000 | - | 0.0000 |
| A. Clean magnitude vs analog (LP, High resolution, low bands): worst error, target >= -24 dB (19 cases) | - | 0.0268 | - | 0.0097 | - | 0.0097 |
| A. Clean magnitude vs analog (LP, High resolution, low bands): worst error, cuts -24..-48 dB | - | 5.13e-05 | - | 0.000274 | - | 0.000269 |
| A. Clean magnitude vs analog (LP, High resolution, low bands): cases outside tolerance | - | 0.0000 | - | 0.0000 | - | 0.0000 |
| P. Factory presets: number of factory presets | - | - | - | 42.000 | - | - |
| P. Factory presets: every preset value refers to a real parameter id | - | - | - | 1.000 | - | - |
| P. Factory presets: names/descriptions: no protected names, description present | - | - | - | 1.000 | - | - |
| P. Factory presets: Vocals presets | - | - | - | 14.000 | - | - |
| P. Factory presets: Beats & Instruments presets | - | - | - | 13.000 | - | - |
| P. Factory presets: Mix Bus & Master presets | - | - | - | 10.000 | - | - |
| P. Factory presets: Axell Signature presets | - | - | - | 4.000 | - | - |
| P. Factory presets: all presets: output finite on pink noise | - | - | - | 1.000 | - | - |
| P. Factory presets: loudness vs bypass, pink noise -18 dBFS, K-weighted (worst: Master - Air & Clarity) | - | - | - | 0.0484 | - | - |
| K. Real-time safety: heap allocations inside setParams()+process() (all modes/OS, automation, live mode switch) | - | - | - | 0.0000 | - | - |
| J. CPU (48 kHz, 128-sample blocks, stereo): vocal: 7 bands (2 dynamic) + HP, Zero Latency | - | - | - | 1.456 | - | - |
| J. CPU (48 kHz, 128-sample blocks, stereo): beat bus: 6 bands + Console + Passive Tube, OS 2x | - | - | - | 2.974 | - | - |
| J. CPU (48 kHz, 128-sample blocks, stereo): master: 8 bands (M/S), Natural Phase + Air | - | - | - | 1.612 | - | - |
| J. CPU (48 kHz, 128-sample blocks, stereo): master: same in Linear Phase (Medium) | - | - | - | 1.329 | - | - |
| J. CPU (48 kHz, 128-sample blocks, stereo): vocal + 2 spectral bands (harshness 3 kHz, sibilance 7 kHz) | - | - | - | 2.761 | - | - |
| J. CPU (48 kHz, 128-sample blocks, stereo): near Nyquist: 4 fitted bands at 48 kHz (2 dynamic, gain moving) | - | - | - | 4.340 | - | - |
| J. CPU (48 kHz, 128-sample blocks, stereo): worst case: 12 dynamic Character bands + all modules, OS 4x | - | - | - | 13.320 | - | - |
