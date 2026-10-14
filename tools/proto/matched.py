# Prototype: matched-response biquads (impulse-invariant poles + magnitude-matched numerator)
import numpy as np
def analog(num, den, w):  # w in rad/sample, s normalized
    s = 1j*w
    return (num[0]*s**2+num[1]*s+num[2])/(den[0]*s**2+den[1]*s+den[2])
def poles_digital(den):
    r = np.roots(den)
    z = np.exp(r)
    a = np.real(np.poly(z))  # 1, a1, a2
    return a
def phis(w):
    p1 = np.sin(w/2)**2; p0 = 1-p1; return p0, p1, 4*p0*p1
def Avec(a):
    return (1+a[1]+a[2])**2, (1-a[1]+a[2])**2, -4*a[2]
def magsq_from(B, w):
    p0,p1,p2 = phis(w); return B[0]*p0+B[1]*p1+B[2]*p2
def factor(B):
    B0,B1,B2 = B
    for s1 in (1,-1):
        W = 0.5*(np.sqrt(max(B0,0))+s1*np.sqrt(max(B1,0)))
        d = W*W+B2
        if d >= 0:
            b0 = 0.5*(W+np.sqrt(d)); b1 = 0.5*(np.sqrt(max(B0,0))-s1*np.sqrt(max(B1,0)))
            b2 = -B2/(4*b0) if b0!=0 else 0
            return np.array([b0,b1,b2])
    return None
def three_point(num, den, wm):
    a = poles_digital(den); A = Avec(a)
    H2 = lambda w: abs(analog(num,den,w))**2
    D = lambda w: magsq_from(A,w)
    B0 = H2(1e-9)*A[0]
    B1 = H2(np.pi)*A[1]
    p0,p1,p2 = phis(wm)
    B2 = (H2(wm)*D(wm) - B0*p0 - B1*p1)/p2
    return factor((B0,B1,B2)), a
def ls_fit(num, den, w0, n=48):
    # weighted LS on relative power error, exact at DC and Nyquist enforced via weights
    a = poles_digital(den); A = Avec(a)
    ws = np.concatenate([[1e-6], np.geomspace(max(w0/40,1e-4), np.pi*0.999, n), [np.pi]])
    H2 = np.abs(analog(num,den,ws))**2
    p0,p1,p2 = phis(ws); D = A[0]*p0+A[1]*p1+A[2]*p2
    M = np.stack([p0,p1,p2],1); y = H2*D
    wt = 1/np.maximum(y,1e-30); wt[0]*=1e4; wt[-1]*=1e4
    B = np.linalg.lstsq(M*wt[:,None], y*wt, rcond=None)[0]
    return factor(B), a
def digital(b,a,w):
    z = np.exp(-1j*w)
    return (b[0]+b[1]*z+b[2]*z*z)/(a[0]+a[1]*z+a[2]*z*z)
def err(num,den,b,a,fs,fmax):
    f = np.geomspace(20, fmax, 600); w = 2*np.pi*f/fs
    return np.max(np.abs(20*np.log10(np.abs(digital(b,a,w))/np.abs(analog(num,den,w)))))
def proto(kind, f0, Q, gdb, fs):
    w0 = 2*np.pi*f0/fs; A = 10**(gdb/40); 
    # normalized s' = s/w0 -> substitute s/w0: polynomials in s with w0 scaling
    if kind=='bell':   n=[1, A/Q, 1]; d=[1, 1/(A*Q), 1]
    elif kind=='ls':   n=[A, np.sqrt(A)/Q*A, A*A]; d=[A, np.sqrt(A)/Q, 1]   # RBJ analog low shelf*... 
    elif kind=='hs':   n=[A*A, np.sqrt(A)/Q*A, A]; d=[1, np.sqrt(A)/Q, A]
    elif kind=='lp':   n=[0,0,1]; d=[1,1/Q,1]
    elif kind=='hp':   n=[1,0,0]; d=[1,1/Q,1]
    elif kind=='bp':   n=[0,1/Q,0]; d=[1,1/Q,1]
    # scale to rad/sample: s_norm = s/w0 -> coefficient of s^2 divided by w0^2, s by w0
    sc = lambda p: [p[0]/w0**2, p[1]/w0, p[2]]
    return sc(n), sc(d), w0
if __name__=='__main__':
    for fs in (44100, 48000, 96000):
        fmax = min(20000, 0.95*fs/2)
        for kind in ('bell','ls','hs','lp','hp','bp'):
            worst = {'3pt':0,'ls':0}; wc={}
            for f0 in np.geomspace(20, 20000, 25):
                for Q in (0.3,0.5,0.707,1,2,4,8,16):
                    for g in ((-24,-12,-6,-1,1,6,12,24) if kind in ('bell','ls','hs') else (0,)):
                        if kind in ('lp','hp','bp') and Q>8: continue
                        n,d,w0 = proto(kind,f0,Q,g,fs)
                        for m,fn in (('3pt',lambda: three_point(n,d,w0 if kind not in ('ls','hs') else w0)),('ls',lambda: ls_fit(n,d,w0))):
                            b,a = fn()
                            e = 99 if b is None else err(n,d,b,a,fs,fmax)
                            if kind in ('lp','hp','bp'):
                                pass
                            if e>worst[m]: worst[m]=e; wc[m]=(round(f0),Q,g)
            print(fs, kind, {k:(round(v,3),wc.get(k)) for k,v in worst.items()})
