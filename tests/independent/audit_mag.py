"""Independent audit, part 1: Clean-mode magnitude and phase of every band type, random settings, six sample
rates, through the built VST3, against textbook analog curves computed with scipy (tests/independent/README.md).

Pass criterion = the spec: within 0.1 dB where the analog curve is >= -24 dB, cuts within 0.5 dB down to -48 dB.
Settings outside the plugin's documented accuracy envelope are reported separately as "known limits" (README,
"Accuracy"), never silently dropped. Exit code = number of failures."""
import sys, json, math, numpy as np
from concurrent.futures import ThreadPoolExecutor
from scipy import signal
from lib import *

SLOPES = [6, 12, 18, 24, 36, 48, 72, 96]
TYPES = ["Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt"]

def cut_ref(f, f0, order, high, res):
    """Butterworth from scipy's own pole placement; the resonance rescales the lowest-Q pole pair (the
    plugin's documented definition; 0.7071 = plain Butterworth)"""
    z, p, k = signal.buttap(order)
    pairs = sorted([x for x in p if x.imag > 1e-12], key=lambda x: abs(x) / (-2 * x.real))
    real = [x for x in p if abs(x.imag) <= 1e-12]
    s = 1j * f / f0
    h = np.ones_like(s)
    for i, x in enumerate(pairs):
        q = abs(x) / (-2 * x.real)
        if i == 0:
            q *= res / math.sqrt(0.5)
        h *= (s * s if high else 1) / (s * s + s / q + 1)
    for x in real:
        h *= (s if high else 1) / (s + 1)
    return h

def reference(c, f):
    t, f0, g, q, sl = c["type"], c["f"], c["g"], c["q"], c["slope"]
    if t == "Bell": return H_bell(f, f0, q, g)
    if t in ("Low Shelf", "High Shelf"):
        low = t == "Low Shelf"
        return H_shelf1(f, f0, g, low) if sl == 6 else H_shelf2(f, f0, q, g, low)
    if t in ("Low Cut", "High Cut"):
        return cut_ref(f, f0, sl // 6, t == "Low Cut", min(max(q, 0.3), 12))
    if t == "Notch": return H_notch(f, f0, q)  # the analog curve at its true centre, also above Nyquist
    if t == "Band Pass": return H_bp(f, f0, q, min(max((sl + 6) // 12, 1), 8))
    if t == "Tilt": return H_tilt1(f, f0, g) if sl == 6 else H_tilt2(f, f0, q, g)

def known_limit(c, phase):
    """documented accuracy envelope (README "Accuracy"); returns the reason a setting lies outside, or None.
    Since 2.0 the near-Nyquist limits at 44.1/48 kHz are gone (fitted 4th-order realisations, DECISIONS D25):
    every band type, Q and frequency up to 30 kHz is checked against the spec at every rate. No setting is
    exempt any more; the hook stays for future features."""
    # (2.0 also removed the last one: Natural Phase band-pass >= 72 dB/oct with Q > 6, D26)
    return None

def make_configs(seed, fss, per, flo=20.0, fhi=20000.0, shelfq=(0.3, 4.0)):
    rng = np.random.default_rng(seed)
    cfgs = []
    for fs in fss:
        for t in TYPES:
            for i in range(per[t]):
                c = dict(fs=fs, type=t, f=float(np.exp(rng.uniform(np.log(flo), np.log(fhi)))),
                         g=float(rng.uniform(-30, 30)), q=float(np.exp(rng.uniform(np.log(0.1), np.log(24)))),
                         slope=int(rng.choice(SLOPES)))
                if t in ("Low Shelf", "High Shelf", "Tilt"):
                    c["slope"] = int(rng.choice([6, 12]))
                    c["q"] = float(np.exp(rng.uniform(np.log(shelfq[0]), np.log(shelfq[1]))))
                if t in ("Low Cut", "High Cut"):
                    c["q"] = float(np.exp(rng.uniform(np.log(0.3), np.log(4)))) if rng.uniform() < 0.5 else 0.70710678
                if t == "Band Pass":
                    c["q"] = float(np.exp(rng.uniform(np.log(0.3), np.log(12))))
                cfgs.append(c)
    return cfgs

def measure(cfgs, phase, tag, workers=12):
    runs, inputs = [], {}
    for i, c in enumerate(cfgs):
        fs = c["fs"]
        n = int(2 ** math.ceil(math.log2(fs * 3.0)))
        sets = {"Phase Mode": phase, "Oversampling": f'{c.get("os", 1)}x', "Band 1 Active": "On", "Band 1 Type": c["type"],
                "Band 1 Frequency": f'{c["f"]:.6f}', "Band 1 Gain": f'{c["g"]:.6f}', "Band 1 Q": f'{c["q"]:.6f}',
                "Band 1 Slope": f'{c["slope"]} dB/oct'}
        r = Run(f"{tag}_{i}", fs, ch=1, sets=sets)
        runs.append(r); inputs[r.name] = impulse(n, 1)
    chunks = [runs[i::workers] for i in range(workers)]
    outs = {}
    with ThreadPoolExecutor(workers) as ex:
        for o in ex.map(lambda ch: run_all(ch, inputs, jobname=f"{tag}_{id(ch)}"), chunks):
            outs.update(o)
    res = []
    for c, r in zip(cfgs, runs):
        y = outs[r.name][:, 0]
        fs, n = c["fs"], len(y)
        Y = np.fft.rfft(y)
        k = np.unique(np.round(logf(fs, n=300) * n / fs).astype(int))
        f = k * fs / n
        H = Y[k] * np.exp(2j * np.pi * k * r.latency / n)  # exact DFT bins, latency removed
        R = reference(c, f)
        dH, dR = db(H), db(R)
        cut = c["type"] in ("Low Cut", "High Cut")
        e24 = float(np.abs(np.where(dR >= -24, dH - dR, 0.0)).max())
        e48 = float(np.abs(np.where((dR < -24) & (dR >= -48), dH - dR, 0.0)).max()) if cut else 0.0
        pm = dR >= -20
        ph = float(np.abs(np.degrees(np.angle(H[pm] / R[pm]))).max()) if pm.any() else 0.0
        res.append(dict(c, phase=phase, lat=r.latency, e24=e24, e48=e48, ph=ph, limit=known_limit(c, phase)))
    return res

def summary(res, label):
    inside = [r for r in res if r["limit"] is None]
    fails = [r for r in inside if r["e24"] > 0.1 or r["e48"] > 0.5]
    print(f"\n== {label}: {len(inside) - len(fails)}/{len(inside)} within spec, worst {max([r['e24'] for r in inside] + [0]):.4f} dB "
          f"(>= -24 dB), worst cut skirt {max([r['e48'] for r in inside] + [0]):.3f} dB")
    if res[0]["phase"] == "Natural Phase":
        print(f"   phase vs analog (>= -20 dB): worst {max([r['ph'] for r in inside] + [0]):.2f} deg")
    by = {}
    for r in inside:
        by.setdefault(r["type"], []).append(r["e24"])
    for t, v in by.items():
        print(f"   {t:10s} n={len(v):3d} worst {max(v):.5f} dB  median {np.median(v):.6f}")
    for r in fails:
        print(f"   FAIL fs={r['fs']} {r['type']} f={r['f']:.1f} g={r['g']:+.2f} q={r['q']:.3f} slope={r['slope']} "
              f"e24={r['e24']:.3f} e48={r['e48']:.3f}")
    lim = [r for r in res if r["limit"] is not None]
    if lim:
        print(f"   known limits ({len(lim)} settings outside the documented envelope, not counted):")
        for reason in sorted(set(r["limit"] for r in lim)):
            v = [r for r in lim if r["limit"] == reason]
            print(f"     - {reason}: {len(v)} settings, worst {max(r['e24'] for r in v):.3f} dB")
    return len(fails)

if __name__ == "__main__":
    fss = [44100, 48000, 88200, 96000, 176400, 192000]
    per = {"Bell": 24, "Low Shelf": 10, "High Shelf": 10, "Low Cut": 12, "High Cut": 12, "Notch": 6, "Band Pass": 8, "Tilt": 8}
    cfgs = make_configs(2026, fss, per)
    fails = summary(measure([dict(c) for c in cfgs], "Zero Latency", "zl"), "Zero Latency, all band types, 6 rates")
    sub = [dict(c) for c in cfgs if c["fs"] in (44100, 48000, 96000)]
    fails += summary(measure(sub, "Natural Phase", "np"), "Natural Phase, all band types, 44.1/48/96 kHz")
    # resonant shelves/tilt over the whole Q range; at 44.1/48 kHz the Q > 5 ones again with 2x oversampling
    rng = np.random.default_rng(7)
    sh = []
    for fs in (44100, 48000, 96000):
        for t in ("Low Shelf", "High Shelf", "Tilt"):
            for i in range(30):
                sh.append(dict(fs=fs, type=t, f=float(np.exp(rng.uniform(np.log(20), np.log(20000)))), g=float(rng.uniform(-30, 30)),
                               q=float(np.exp(rng.uniform(np.log(0.1), np.log(24)))), slope=12))
    sh += [dict(c, os=2) for c in sh if c["q"] > 5 and c["fs"] < 80000]
    fails += summary(measure(sh, "Zero Latency", "sh"), "shelves/tilt Q 0.1-24 at 44.1/48/96 kHz (Q > 5 at 44.1/48 kHz also with OS 2x)")
    # band frequencies up to 30 kHz (the control allows it), every type, also high-Q shelves/tilt and both modes
    hi = make_configs(99, [44100, 48000], {t: 12 for t in TYPES}, flo=12000, fhi=30000, shelfq=(0.1, 24.0))
    fails += summary(measure([dict(c) for c in hi], "Zero Latency", "hi"), "band frequencies 12-30 kHz at 44.1/48 kHz (Zero Latency)")
    fails += summary(measure([dict(c) for c in hi], "Natural Phase", "hn"), "band frequencies 12-30 kHz at 44.1/48 kHz (Natural Phase)")
    print(f"\nAUDIT 1 (magnitude/phase): {fails} failures")
    sys.exit(fails)
