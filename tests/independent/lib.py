"""Independent audit helpers: drive a built .vst3 through PentaAuditHost (JUCE's VST3 hosting code), and
textbook analog references evaluated with scipy - nothing here uses the plugin's own code or models.
Environment: PENTA_AUDIT_HOST (host binary), PENTA_VST3 (plugin bundle), PENTA_AUDIT_WORK (scratch dir)."""
import os, subprocess, re, tempfile, numpy as np
from scipy import signal

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOST = os.environ.get("PENTA_AUDIT_HOST", os.path.join(ROOT, "build/PentaAuditHost_artefacts/Release/PentaAuditHost"))
PLUGIN = os.environ.get("PENTA_VST3", os.path.join(ROOT, "build/AxellPentaEQ_artefacts/Release/VST3/Axell Penta EQ.vst3"))
WORK = os.environ.get("PENTA_AUDIT_WORK", os.path.join(tempfile.gettempdir(), "penta-audit"))
os.makedirs(WORK, exist_ok=True)

def raw_write(path, x, fs):
    x = np.asarray(x, dtype=np.float32)
    if x.ndim == 1:
        x = x[:, None]
    x.tofile(path)

def raw_read(path, ch):
    return np.fromfile(path, dtype=np.float32).astype(np.float64).reshape(-1, ch)

class Run:
    def __init__(self, name, fs=48000, ch=2, sets=None, ats=None, blocks="512", nonrt=True,
                 preroll=None, sleep=0, pdc=0):
        self.name, self.fs, self.ch = name, fs, ch
        self.sets = dict(sets or {})
        self.ats = list(ats or [])
        self.blocks, self.nonrt, self.sleep, self.pdc = blocks, nonrt, sleep, pdc
        self.preroll = preroll if preroll is not None else int(0.5 * fs)
        self.inp = os.path.join(WORK, name + "_in.f32")
        self.out = os.path.join(WORK, name + "_out.f32")
        self.sc = None
        self.latency = None
        self.log = ""

    def job(self):
        L = [f"rate {self.fs}", f"channels {self.ch}", f"blocks {self.blocks}",
             f"nonrealtime {1 if self.nonrt else 0}", f"preroll {self.preroll}", f"sleep {self.sleep}", f"pdc {self.pdc}"]
        L += [f"set {k}={v}" for k, v in self.sets.items()]
        L += [f"at {a} set {k}={v}" for a, k, v in self.ats]
        L += [f"input {self.inp}", f"output {self.out}"]
        if self.sc:
            L.append(f"sidechain {self.sc}")
        L.append("run")
        return "\n".join(L)

def run_all(runs, inputs, sidechains=None, jobname="job"):
    """inputs: dict name -> array (n,ch) ; writes, runs host once, returns dict name -> output array"""
    for r in runs:
        raw_write(r.inp, inputs[r.name], r.fs)
        if sidechains and r.name in sidechains:
            r.sc = os.path.join(WORK, r.name + "_sc.f32")
            raw_write(r.sc, sidechains[r.name], r.fs)
    jp = os.path.join(WORK, jobname + ".txt")
    with open(jp, "w") as f:
        f.write("\n".join(r.job() for r in runs) + "\n")
    p = subprocess.run([HOST, PLUGIN, jp], capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(p.stdout[-3000:] + p.stderr[-3000:])
    blocks = p.stdout.split("\nrun ")
    lat = re.findall(r"  latency (-?\d+)", p.stdout)
    assert len(lat) == len(runs), p.stdout[-2000:]
    out = {}
    for r, l, b in zip(runs, lat, blocks[1:]):
        r.latency = int(l)
        r.log = b
        out[r.name] = raw_read(r.out, r.ch)
        for path in (r.inp, r.out, r.sc):  # long impulse responses add up to gigabytes: keep nothing
            if path and os.path.exists(path):
                os.remove(path)
    os.remove(jp)
    return out

# ----------------------------------------------------------------------------------------------
# Independent analog references (textbook forms, evaluated with scipy.signal.freqs)
def H_bell(f, f0, Q, g):
    A = 10 ** (abs(g) / 40)
    w0 = 2 * np.pi * f0
    b = [1 / w0**2, A / (Q * w0), 1]; a = [1 / w0**2, 1 / (A * Q * w0), 1]
    if g < 0: b, a = a, b
    return signal.freqs(b, a, 2 * np.pi * f)[1]

def H_shelf2(f, f0, Q, g, low):
    # RBJ cookbook analog shelves (s normalised to w0)
    A = 10 ** (abs(g) / 40); sa = np.sqrt(A)
    s = 1j * f / f0
    if low:
        h = A * (s**2 + sa / Q * s + A) / (A * s**2 + sa / Q * s + 1)
    else:
        h = A * (A * s**2 + sa / Q * s + 1) / (s**2 + sa / Q * s + A)
    return h if g >= 0 else 1 / h

def H_shelf1(f, f0, g, low):
    T = 10 ** (abs(g) / 20); st = np.sqrt(T)
    s = 1j * f / f0
    h = (s + st) / (s + 1 / st) if low else T * (s + 1 / st) / (s + st)
    return h if g >= 0 else 1 / h

def H_butter(f, f0, order, high, res=None):
    """Butterworth via scipy (independent pole placement); res rescales one pole pair's Q"""
    z, p, k = signal.butter(order, 2 * np.pi * f0, btype="high" if high else "low", analog=True, output="zpk")
    return signal.freqs_zpk(z, p, k, 2 * np.pi * f)[1]

def H_notch(f, f0, Q):
    s = 1j * f / f0
    return (s**2 + 1) / (s**2 + s / Q + 1)

def H_bp(f, f0, Q, n=1):
    s = 1j * f / f0
    return ((s / Q) / (s**2 + s / Q + 1)) ** n

def H_tilt1(f, f0, g):
    # -g/2 below, +g/2 above, 0 dB at f0
    return H_shelf1(f, f0, g, low=False) / 10 ** (g / 40)

def H_tilt2(f, f0, Q, g):
    return H_shelf2(f, f0, Q, g, low=False) / 10 ** (g / 40)

def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-30))

def response(y, latency, nfft=None):
    """transfer function of an impulse response, latency removed"""
    y = np.asarray(y, dtype=np.float64)
    n = len(y) if nfft is None else nfft
    Y = np.fft.rfft(y, n)
    k = np.arange(len(Y))
    Y = Y * np.exp(2j * np.pi * k * latency / n)
    return Y

def logf(fs, lo=20.0, hi=20000.0, n=400):
    hi = min(hi, 0.4535 * fs)  # 20 kHz at 44.1 kHz
    return np.geomspace(lo, hi, n)

def dtft(y, fs, f, latency=0):
    """exact DTFT at arbitrary frequencies (no bin interpolation), latency removed"""
    y = np.asarray(y, dtype=np.float64)
    n = np.arange(len(y))
    w = 2 * np.pi * np.asarray(f) / fs
    # chunked to bound memory
    out = np.zeros(len(w), dtype=complex)
    for i in range(0, len(w), 16):
        ww = w[i:i + 16, None]
        out[i:i + 16] = (y[None, :] * np.exp(-1j * ww * (n[None, :] - latency))).sum(axis=1)
    return out

def impulse(n, ch=2, amp=1.0):
    x = np.zeros((n, ch)); x[0, :] = amp
    return x
