"""Independent audit, part 2: processing fidelity of the built VST3 through a real VST3 host - transparency,
reported vs actual latency, linearity, host block-size invariance, stereo/M-S, oversampling, mix alignment, linear
phase, click-free automation and mode switches, dynamics, side-chain, silence (tests/independent/README.md).
Exit code = number of failures."""
import numpy as np, math, re, sys
from scipy import signal as sg
from lib import *

results = []
def report(name, ok, detail):
    results.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}", flush=True)

def noise(n, ch, seed=1, amp=0.25):
    return np.random.default_rng(seed).uniform(-amp, amp, (n, ch))

def band(i, **kw):
    d = {f"Band {i} Active": "On"}
    for k, v in kw.items():
        d[f"Band {i} {k}"] = v
    return d

RATES = [44100, 48000, 88200, 96000, 176400, 192000]
PHASES = ["Zero Latency", "Natural Phase", "Linear Phase"]

# ---------------------------------------------------------------- 1. transparency + reported latency
runs, ins = [], {}
for fs in RATES:
    for ph in PHASES:
        for os_ in ["1x", "2x", "4x"]:
            r = Run(f"tr_{fs}_{ph[0]}_{os_}", fs, sets={"Phase Mode": ph, "Oversampling": os_})
            runs.append(r); ins[r.name] = sg.sosfiltfilt(sg.butter(12, 20000, fs=fs, output="sos"), noise(fs // 2, 2, 3), axis=0)
out = run_all(runs, ins, jobname="transp")
for r in runs:
    x, y, L = ins[r.name], out[r.name], r.latency
    e_full = y[L:] - x[:len(x) - L]
    err = np.abs(e_full[2000:-2000]).max()  # skip the silence -> noise onset (broadband step)
    os_ = r.name.split("_")[-1]
    # no bands: expected bit-exact (1x) or the oversampler's passband (2x/4x)
    tol = 2e-8 if os_ == "1x" else 10 ** (-60 / 20)  # 1x: float rounding only (input is band-limited to 20 kHz)
    report(f"default settings transparent ({r.fs}, {r.sets['Phase Mode']}, OS {os_})", err <= tol + 1e-12,
           f"latency {L}, max |out - in delayed| = {err:.3g} ({20*np.log10(err+1e-30):.1f} dBFS)")

# ---------------------------------------------------------------- 2. latency measured == reported, with a band
runs, ins = [], {}
for fs in RATES:
    for ph in PHASES:
        for os_ in ["1x", "2x", "4x"]:
            r = Run(f"lat_{fs}_{ph[0]}_{os_}", fs, ch=1, sets={"Phase Mode": ph, "Oversampling": os_, **band(1, Type="Bell", Frequency="1000", Gain="6", Q="1")})
            n = 1 << 16
            runs.append(r); ins[r.name] = impulse(n, 1)
out = run_all(runs, ins, jobname="lat")
for r in runs:
    y = out[r.name][:, 0]
    fs = r.fs
    # group delay at 8 kHz (far from the band, bell phase ~ 0 in natural/linear; min-phase small)
    f = np.array([4000.0, 8000.0, 12000.0])
    H = dtft(y, fs, f, r.latency)
    mode = r.sets["Phase Mode"]
    R = H_bell(f, 1000, 1, 6)
    # Zero Latency / Natural Phase follow the analog (minimum) phase, Linear Phase is zero phase
    ph = np.degrees(np.angle(H / R)) if mode != "Linear Phase" else np.degrees(np.angle(H))
    ok = np.abs(ph).max() < 1.0 and (r.latency == 0 or mode != "Zero Latency" or r.sets["Oversampling"] != "1x")
    report(f"{mode}: reported latency {r.latency} matches the signal ({fs}, OS {r.sets['Oversampling']})", ok,
           f"phase residual at 4/8/12 kHz {np.round(ph, 3)} deg")

# ---------------------------------------------------------------- 3. linearity / LTI: noise output == impulse response * noise
for fs in [44100, 96000]:
    for ph in PHASES:
        sets = {"Phase Mode": ph, "Drive": "100", **band(1, Type="Bell", Frequency="120", Gain="9", Q="0.8"),
                **band(2, Type="High Shelf", Frequency="9000", Gain="-6", Q="0.7"), **band(3, Type="Low Cut", Frequency="40", Slope="24 dB/oct"),
                **band(4, Type="Notch", Frequency="3000", Q="6")}
        n = fs * 2
        r1 = Run(f"lti_ir_{fs}_{ph[0]}", fs, ch=1, sets=sets); r2 = Run(f"lti_nz_{fs}_{ph[0]}", fs, ch=1, sets=sets)
        x = noise(n, 1, 9, amp=1.5)  # +3.5 dBFS peaks: clean path must not clip or saturate
        o = run_all([r1, r2], {r1.name: impulse(n, 1), r2.name: x}, jobname="lti")
        h = o[r1.name][:, 0]
        pred = np.convolve(x[:, 0], h)[:n]
        e = o[r2.name][:, 0] - pred
        rel = 20 * np.log10(np.abs(e[fs // 2:]).max() / np.abs(pred[fs // 2:]).max())
        report(f"Clean bands are linear and time-invariant at Drive 100, +3.5 dBFS input ({fs}, {ph})", rel < -100,
               f"residual vs convolution {rel:.1f} dB")

# ---------------------------------------------------------------- 4. block-size invariance (random host blocks 1..4096)
for ph in PHASES:
    sets = {"Phase Mode": ph, "Oversampling": "2x", **band(1, Type="Bell", Frequency="300", Gain="-4", Q="2", Dynamic="On", Threshold="-30"),
            **band(2, Type="High Shelf", Frequency="6000", Gain="5", Mode="Character"), "Inductor On": "On", "Inductor Low Gain": "4"}
    fs = 48000
    x = noise(fs * 2, 2, 4)
    ra = Run(f"blk_a_{ph[0]}", fs, sets=sets, blocks="512")
    rb = Run(f"blk_b_{ph[0]}", fs, sets=sets, blocks="random 1 4096 5")
    rc = Run(f"blk_c_{ph[0]}", fs, sets=sets, blocks="random 1 64 6")
    o = run_all([ra, rb, rc], {ra.name: x, rb.name: x, rc.name: x}, jobname="blk")
    d1 = np.abs(o[ra.name] - o[rb.name]).max(); d2 = np.abs(o[ra.name] - o[rc.name]).max()
    report(f"host block size does not change the output ({ph}, dynamic + character + OS 2x)", d1 == 0 and d2 == 0,
           f"max diff 512 vs random(1..4096) {d1:.3g}, vs random(1..64) {d2:.3g}")

# ---------------------------------------------------------------- 5. stereo / placement
fs = 48000; n = 1 << 15
imp = np.zeros((n, 2)); imp[0] = [1.0, 1.0]
imps = np.zeros((n, 2)); imps[0] = [1.0, -1.0]
bell = dict(Type="Bell", Frequency="2000", Gain="8", Q="1.5")
tests = {
    "pl_mono_ref": ({**band(1, **bell)}, imp),
    "pl_mid_LR": ({**band(1, **bell, Placement="Mid")}, imp),       # L=R: mid band == stereo band
    "pl_side_LR": ({**band(1, **bell, Placement="Side")}, imp),     # L=R: side band does nothing
    "pl_side_anti": ({**band(1, **bell, Placement="Side")}, imps),  # L=-R: side band == stereo band
    "pl_mid_anti": ({**band(1, **bell, Placement="Mid")}, imps),    # L=-R: mid band does nothing
    "pl_left": ({**band(1, **bell, Placement="Left")}, imp),
}
for ph in PHASES:
    runs = [Run(k + "_" + ph[0], fs, sets={"Phase Mode": ph, **s}) for k, (s, _) in tests.items()]
    o = run_all(runs, {r.name: tests[r.name.rsplit("_", 1)[0]][1] for r in runs}, jobname="pl")
    g = {r.name.rsplit("_", 1)[0]: (o[r.name], r.latency) for r in runs}
    ref = g["pl_mono_ref"][0]
    L = g["pl_mono_ref"][1]
    dirac = np.zeros(n); dirac[L] = 1.0
    c = [
        ("Mid band on L=R equals Stereo band", np.abs(g["pl_mid_LR"][0] - ref).max()),
        ("Side band on L=R is transparent", np.abs(g["pl_side_LR"][0] - np.stack([dirac, dirac], 1)).max()),
        ("Side band on L=-R equals Stereo band", np.abs(g["pl_side_anti"][0] - ref * np.array([1, -1])).max()),
        ("Mid band on L=-R is transparent", np.abs(g["pl_mid_anti"][0] - np.stack([dirac, -dirac], 1)).max()),
        ("Left band: L processed, R untouched", max(np.abs(g["pl_left"][0][:, 0] - ref[:, 0]).max(), np.abs(g["pl_left"][0][:, 1] - dirac).max())),
    ]
    for name, e in c:
        report(f"{name} ({ph})", e < 1e-5, f"max error {e:.3g}")

# ---------------------------------------------------------------- 6. oversampling passband (flat EQ, 2x/4x) and a band under OS
runs, ins = [], {}
for fs in [44100, 48000, 96000]:
    for os_ in ["2x", "4x"]:
        for ph in ["Zero Latency", "Natural Phase"]:
            r = Run(f"os_{fs}_{os_}_{ph[0]}", fs, ch=1, sets={"Oversampling": os_, "Phase Mode": ph, **band(1, Type="Bell", Frequency="3000", Gain="6", Q="1")})
            runs.append(r); ins[r.name] = impulse(1 << 16, 1)
out = run_all(runs, ins, jobname="os")
for r in runs:
    y = out[r.name][:, 0]
    f = logf(r.fs, n=300)
    H = dtft(y, r.fs, f, r.latency)
    e = np.abs(db(H) - db(H_bell(f, 3000, 1, 6)))
    report(f"bell under OS {r.sets['Oversampling']} matches analog to 20 kHz ({r.fs}, {r.sets['Phase Mode']})", e.max() < 0.1,
           f"worst {e.max():.4f} dB at {f[np.argmax(e)]:.0f} Hz")

# ---------------------------------------------------------------- 7. mix alignment: Mix 50 % == average of dry (delayed) and wet
for ph in PHASES:
    fs = 48000; n = 1 << 15
    s = {"Phase Mode": ph, **band(1, Type="Bell", Frequency="1000", Gain="12", Q="1")}
    r1 = Run(f"mix_w_{ph[0]}", fs, ch=1, sets=s); r2 = Run(f"mix_h_{ph[0]}", fs, ch=1, sets={**s, "Mix": "50"})
    o = run_all([r1, r2], {r1.name: impulse(n, 1), r2.name: impulse(n, 1)}, jobname="mix")
    dry = np.zeros(n); dry[r1.latency] = 1
    e = np.abs(o[r2.name][:, 0] - 0.5 * (o[r1.name][:, 0] + dry)).max()
    report(f"Mix 50% is the latency-aligned average of dry and wet ({ph})", e < 1e-6 and r1.latency == r2.latency, f"max error {e:.3g}")

# ---------------------------------------------------------------- 8. gain staging exactness
fs = 48000; x = noise(fs, 2, 11)
rs = [Run("g_out", fs, sets={"Output Gain": "6"}), Run("g_in", fs, sets={"Input Gain": "-12"})]
o = run_all(rs, {r.name: x for r in rs}, jobname="gain")
e1 = np.abs(o["g_out"] - x * 10 ** (6 / 20)).max(); e2 = np.abs(o["g_in"] - x * 10 ** (-12 / 20)).max()
report("Output +6 dB and Input -12 dB are exact gains", e1 < 1e-6 and e2 < 1e-6, f"errors {e1:.3g}, {e2:.3g}")

# ---------------------------------------------------------------- 9. linear phase: symmetric impulse response, accuracy by quality
for q, flim in [("Low", 200), ("Medium", 100), ("High", 30)]:
    fs = 48000; n = 1 << 17
    s = {"Phase Mode": "Linear Phase", "Linear Phase Quality": q, **band(1, Type="Bell", Frequency="300", Gain="6", Q="1"),
         **band(2, Type="High Shelf", Frequency="8000", Gain="-4", Q="0.7"), **band(3, Type="Low Cut", Frequency=str(flim), Slope="12 dB/oct", Q="0.70710678")}
    r = Run(f"lp_{q}", fs, ch=1, sets=s)
    y = run_all([r], {r.name: impulse(n, 1)}, jobname="lp")[r.name][:, 0]
    L = r.latency
    seg = y[: 2 * L + 1]
    asym = np.abs(seg - seg[::-1]).max() / np.abs(seg).max()
    f = logf(fs, lo=2 * flim, n=300)
    H = dtft(y, fs, f, L)
    R = H_bell(f, 300, 1, 6) * H_shelf2(f, 8000, 0.7, -4, low=False) * H_butter(f, flim, 2, True)
    e = np.abs(db(H) - db(R)); phe = np.abs(np.degrees(np.angle(H * np.conj(R) / np.abs(R) ** 2 * np.abs(R))))
    phase_lin = np.abs(np.degrees(np.angle(H))).max()
    report(f"Linear Phase {q}: symmetric IR, magnitude vs analog >= {2*flim} Hz", asym < 1e-4 and e.max() < 0.1 and phase_lin < 0.5,
           f"latency {L} ({1000*L/fs:.0f} ms), asymmetry {asym:.2g}, worst mag error {e.max():.3f} dB, max phase {phase_lin:.3f} deg")

# ---------------------------------------------------------------- 10. automation smoothness: fast sweeps on a sine, look for clicks
fs = 48000; n = fs * 3
t = np.arange(n) / fs
sine = 0.3 * np.sin(2 * np.pi * 220 * t)
def sweep(param, vals, every):
    return [(i * every, param, v) for i, v in enumerate(vals)]
cases = {
    "freq sweep 50 Hz -> 15 kHz in 1 s (host steps every 32 samples)": (band(1, Type="Bell", Frequency="50", Gain="12", Q="2"),
        sweep("Band 1 Frequency", [f"{50*300**(i/1500):.3f}" for i in range(1500)], 32)),
    "gain sweep -30 -> +30 dB in 0.5 s": (band(1, Type="Bell", Frequency="220", Gain="-30", Q="1"),
        sweep("Band 1 Gain", [f"{-30+60*i/750:.4f}" for i in range(750)], 32)),
    "gain jumps -30 <-> +30 dB every 100 ms": (band(1, Type="Bell", Frequency="220", Gain="-30", Q="1"),
        sweep("Band 1 Gain", ["30" if i % 2 == 0 else "-30" for i in range(25)], 4800)),
    "type switch Bell <-> High Shelf every 100 ms": (band(1, Type="Bell", Frequency="2000", Gain="9", Q="1"),
        sweep("Band 1 Type", ["High Shelf" if i % 2 == 0 else "Bell" for i in range(25)], 4800)),
    "phase mode switch every 250 ms": ({**band(1, Type="Bell", Frequency="500", Gain="6", Q="1")},
        sweep("Phase Mode", [PHASES[(i + 1) % 3] for i in range(10)], 12000)),
    "oversampling switch every 250 ms": ({**band(1, Type="Bell", Frequency="500", Gain="6", Q="1")},
        sweep("Oversampling", [["2x", "4x", "1x"][i % 3] for i in range(10)], 12000)),
}
for name, (s, ats) in cases.items():
    r = Run("auto", fs, ch=1, sets=s, ats=ats)
    y = run_all([r], {"auto": sine[:, None]}, jobname="auto")["auto"][:, 0]
    # clicks = broadband energy: high-pass the output at 6 kHz (sine at 220 Hz, harmonics none in a linear EQ)
    sos = sg.butter(8, 6000, "high", fs=fs, output="sos")
    hf = sg.sosfilt(sos, y)[fs // 10:]
    lvl = 20 * np.log10(np.abs(hf).max() / np.abs(y[fs // 10:]).max() + 1e-30)
    report(f"no clicks: {name}", lvl < -60, f"peak energy above 6 kHz {lvl:.1f} dB re output peak")

# ---------------------------------------------------------------- 11. dynamics: static gain curve and side-chain
fs = 48000
def sine_at(f, dbfs, secs):
    t = np.arange(int(fs * secs)) / fs
    return (10 ** (dbfs / 20) * np.sin(2 * np.pi * f * t))[:, None]
dyn = band(1, Type="Bell", Frequency="1000", Gain="0", Q="1", Dynamic="On", Threshold="-30", Range="-12", Ratio="4", Attack="5", Release="50")
runs, ins = [], {}
for lv in [-50, -40, -30, -24, -18, -12, -6]:
    r = Run(f"dyn_{-lv}", fs, ch=1, sets=dyn); runs.append(r); ins[r.name] = sine_at(1000, lv, 1.0)
out = run_all(runs, ins, jobname="dyn")
rows = []
for r in runs:
    lv = -int(r.name.split("_")[1])
    y = out[r.name][fs // 2:, 0]; x = ins[r.name][fs // 2:, 0]
    g = 20 * np.log10(np.sqrt(np.mean(y ** 2)) / np.sqrt(np.mean(x ** 2)))
    rows.append((lv, g))
print("   dynamic bell 1 kHz, thr -30, ratio 4, range -12 (sine level in, gain at 1 kHz):", [(a, round(b, 2)) for a, b in rows])
below = [g for lv, g in rows if lv <= -40]
report("dynamic band: no gain change well below threshold", max(abs(g) for g in below) < 0.05, f"{[round(g,3) for g in below]} dB")
deep = [g for lv, g in rows if lv >= -6]
report("dynamic band: reduction limited by Range at high level", all(-12.1 < g < -11.0 for g in deep), f"{[round(g,2) for g in deep]} dB")
mono = all(rows[i + 1][1] <= rows[i][1] + 1e-3 for i in range(len(rows) - 1))
report("dynamic band: gain falls monotonically with level", mono, "")
# external side-chain: kick in SC ducks the band, nothing in SC -> no change
s = {**band(1, Type="Bell", Frequency="60", Gain="0", Q="1", Dynamic="On", Threshold="-30", Range="-9", Ratio="10", Attack="1", Release="80", Detector="External")}
x = sine_at(60, -12, 1.0); x = np.concatenate([x, x], 1)
sc_on = sine_at(60, -6, 1.0); sc_on = np.concatenate([sc_on, sc_on], 1)
r1 = Run("sc_on", fs, ch=2, sets=s); r2 = Run("sc_off", fs, ch=2, sets=s)
o = run_all([r1, r2], {"sc_on": x, "sc_off": x}, sidechains={"sc_on": sc_on, "sc_off": np.zeros_like(sc_on)}, jobname="sc")
g_on = 20 * np.log10(np.sqrt(np.mean(o["sc_on"][fs // 2:, 0] ** 2)) / np.sqrt(np.mean(x[fs // 2:, 0] ** 2)))
g_off = 20 * np.log10(np.sqrt(np.mean(o["sc_off"][fs // 2:, 0] ** 2)) / np.sqrt(np.mean(x[fs // 2:, 0] ** 2)))
report("external side-chain drives the band; silent side-chain leaves it alone", g_on < -8 and abs(g_off) < 0.05, f"with SC {g_on:.2f} dB, silent SC {g_off:.3f} dB")

# ---------------------------------------------------------------- 12. mono == stereo L for a stereo band set
fs = 48000; x = noise(fs, 1, 12)
s = {**band(1, Type="Bell", Frequency="400", Gain="5"), **band(2, Type="High Cut", Frequency="12000", Slope="48 dB/oct"), "Phase Mode": "Natural Phase"}
r1 = Run("m1", fs, ch=1, sets=s); r2 = Run("m2", fs, ch=2, sets=s)
o = run_all([r1, r2], {"m1": x, "m2": np.concatenate([x, x], 1)}, jobname="mono")
e = np.abs(o["m1"][:, 0] - o["m2"][:, 0]).max()
report("mono instance == stereo instance with L=R", e < 1e-6 and r1.latency == r2.latency, f"max diff {e:.3g}")

# ---------------------------------------------------------------- 12b. 2.0: spectral dynamics and Gain Scale
fs = 48000
def tones(freqs_amps, secs=3.0, ch=2):
    t = np.arange(int(fs * secs)) / fs
    x = sum(a * np.sin(2 * np.pi * f * t) for f, a in freqs_amps)
    return np.repeat(x[:, None], ch, axis=1)
def level(y, f, a):
    seg = y[int(1.5 * fs):int(2.8 * fs), 0]
    t = np.arange(len(seg)) / fs
    w = np.hanning(len(seg))
    return 20 * np.log10(2 * np.abs(np.sum(w * seg * np.exp(-2j * np.pi * f * t))) / np.sum(w) / a)
spec = {**band(1, Type="Bell", Frequency="3000", Gain="0", Q="1", Dynamic="On", Threshold="-40", Range="-12", Ratio="20",
               Attack="5", Release="100")}
x = tones([(3000, 0.5), (2200, 0.00316), (300, 0.5)])
runs = []
for mode in ["Spectral", "Broadband"]:
    for ph in PHASES:
        r = Run(f"sp_{mode[0]}_{ph[0]}", fs, sets={**spec, "Band 1 Dynamic Mode": mode, "Phase Mode": ph})
        runs.append(r)
out = run_all(runs, {r.name: x for r in runs}, jobname="spec")
for r in runs:
    y = out[r.name][r.latency:]
    l3, l22, l300 = level(y, 3000, 0.5), level(y, 2200, 0.00316), level(y, 300, 0.5)
    if "Spectral" in r.sets["Band 1 Dynamic Mode"]:
        report(f"spectral dynamics ({r.sets['Phase Mode']}): loud tone cut by the range, quiet tone in the band untouched",
               -12.5 < l3 < -11.5 and abs(l22) < 0.1 and -0.5 < l300 < 0.05,
               f"3 kHz {l3:.2f} dB, 2.2 kHz {l22:.3f} dB, 300 Hz {l300:.2f} dB")
    else:
        report(f"broadband dynamics ({r.sets['Phase Mode']}) for comparison: the quiet tone is pulled down too", l22 < -6.0,
               f"2.2 kHz {l22:.2f} dB")
report("spectral dynamics add no latency (same as broadband in every phase mode)",
       all(runs[i].latency == runs[i + 3].latency for i in range(3)), str([q.latency for q in runs]))
# Gain Scale: a +8 dB bell at 50 % acts as exactly +4 dB, in every phase mode
runs = [Run(f"gs_{ph[0]}", fs, ch=1, sets={**band(1, Type="Bell", Frequency="1000", Gain="8", Q="1"), "Gain Scale": "50", "Phase Mode": ph})
        for ph in PHASES]
out = run_all(runs, {r.name: tones([(1000, 0.1)], ch=1) for r in runs}, jobname="gs")
for r in runs:
    g = level(out[r.name][r.latency:], 1000, 0.1)
    report(f"Gain Scale 50 % ({r.sets['Phase Mode']}): +8 dB bell -> +4 dB", abs(g - 4.0) < 0.05, f"{g:.3f} dB")

# ---------------------------------------------------------------- 13. long silence: output decays to exact zero (no denormal tails / DC)
fs = 48000; x = np.concatenate([noise(fs, 2, 13), np.zeros((fs * 10, 2))])
s = {**band(1, Type="Bell", Frequency="40", Gain="12", Q="12"), **band(2, Type="Low Shelf", Frequency="80", Gain="6"), "Passive Tube On": "On",
     "Passive Tube Low Boost": "5", "Inductor On": "On", "Console On": "On", "Air On": "On"}
r = Run("sil", fs, sets=s)
y = run_all([r], {"sil": x}, jobname="sil")["sil"]
tail = np.abs(y[-fs:]).max()
report("output after 10 s of silence (resonant band + whole character rack)", tail < 1e-20, f"max |y| in last second {tail:.3g}")

# ---------------------------------------------------------------- 14. live latency changes: output aligned with the host's compensation
# A live host (message loop between blocks, ~0.1 s of audio per plugin timer tick) switches the phase mode
# every 0.5 s. At every sample the output must be the input delayed by the latency the host compensates
# *at that moment* (as reported through restartComponent), times a fade gain in [0, 1]; never misaligned.
fs = 48000
seq = ["Natural Phase", "Zero Latency", "Linear Phase", "Zero Latency", "Natural Phase", "Linear Phase", "Natural Phase", "Zero Latency"]
x = noise(int(fs * 0.5 * (len(seq) + 1)), 1, 14)
r = Run("pdc", fs, ch=1, sets={"Phase Mode": "Zero Latency"}, ats=[((i + 1) * (fs // 2), "Phase Mode", v) for i, v in enumerate(seq)],
        nonrt=False, pdc=2, preroll=fs // 4)
y = run_all([r], {"pdc": x}, jobname="pdc")["pdc"][:, 0]
ev = [(int(a), int(b)) for a, b in re.findall(r"pdc (\d+) (-?\d+)", r.log)]
Lh = np.zeros(len(y), int)
for k, (pos, lat) in enumerate(ev):
    Lh[pos:] = lat
idx = np.arange(len(y)) - Lh
xd = np.where(idx >= 0, x[np.clip(idx, 0, None), 0], 0.0)
W = 16; nw = len(y) // W
Y, X = y[:nw * W].reshape(nw, W), xd[:nw * W].reshape(nw, W)
g = (Y * X).sum(1) / np.maximum((X * X).sum(1), 1e-12)
res = np.abs(Y - g[:, None] * X).max(1)
bad = (res > 0.02) | (g < -0.01) | (g > 1.01)
audible = (np.abs(Y).max(1) > 1e-3).mean()
lats = sorted(set(l for _, l in ev))
report("live latency changes: output always aligned with the host's current compensation", bad.sum() == 0 and audible > 0.6 and len(lats) == 3,
       f"{len(ev)} host latency updates {lats}, misaligned 16-sample windows {int(bad.sum())}, audible {100 * audible:.0f} %")

fails = [r for r in results if not r[1]]
print(f"\nAUDIT 2 (processing): {len(results) - len(fails)}/{len(results)} pass")
for n_, ok, d in fails:
    print("  FAIL", n_, d)
sys.exit(len(fails))
