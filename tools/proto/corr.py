import numpy as np
from matched import analog, poles_digital, Avec, phis, factor, digital, proto, three_point
def vic_peak(n,d,w0,G):
    a = poles_digital(d); A = Avec(a); p0,p1,p2 = phis(w0)
    B0=A[0]; R1=(A[0]*p0+A[1]*p1+A[2]*p2)*G*G; R2=(-A[0]+A[1]+4*(p0-p1)*A[2])*G*G
    B2=(R1-R2*p1-B0)/(4*p1*p1); B1=R2+B0+4*(p1-p0)*B2
    return factor((B0,B1,B2)),a
def fit_fixed_poles(target2, a, ws, wt_extra=None):
    A = Avec(a); p0,p1,p2 = phis(ws); D = A[0]*p0+A[1]*p1+A[2]*p2
    y = target2*D; M=np.stack([p0,p1,p2],1); wt=1/np.maximum(y,1e-300)
    if wt_extra is not None: wt=wt*wt_extra
    B=np.linalg.lstsq(M*wt[:,None], y*wt, rcond=None)[0]
    return factor(B)
def design(kind,f0,Q,g,fs, wc=0.8, qc=0.6):
    n,d,w0 = proto(kind,f0,Q,abs(g),fs)
    if kind=='bell': b1,a1 = vic_peak(n,d,w0,10**(abs(g)/20))
    else: b1,a1 = three_point(n,d,w0)
    # residual correction: poles at wc*pi
    ws = np.concatenate([[1e-5], np.geomspace(2*np.pi*15/fs, np.pi, 80)])
    R2 = (np.abs(analog(n,d,ws))/np.abs(digital(b1,a1,ws)))**2
    wcr = wc*np.pi
    a2 = poles_digital([1/wcr**2, 1/(qc*wcr), 1])
    ex = np.ones_like(ws); ex[0]=1e3; ex[-1]=1e2
    b2 = fit_fixed_poles(R2, a2, ws, ex)
    return n,d,(b1,a1),(b2,a2)
def errc(n,d,s1,s2,fs,fmax):
    f=np.geomspace(20,fmax,800); w=2*np.pi*f/fs
    Hd = digital(*s1,w)*(digital(*s2,w) if s2 is not None and s2[0] is not None else 1)
    return np.max(np.abs(20*np.log10(np.abs(Hd)/np.abs(analog(n,d,w)))))
if __name__=='__main__':
  fr=np.geomspace(20,20000,41)
  for fs in (44100,48000,96000):
    fmax=min(20000,0.95*fs/2)
    for kind in ('bell','ls','hs'):
      Qs=(0.1,0.3,0.5,0.707,1,2,4,8,18) if kind=='bell' else (0.3,0.5,0.707,1,1.5,2)
      for wc,qc in ((0.8,0.6),(0.7,0.5),(0.9,0.7)):
        fails=0; worst=(0,None); tot=0
        for f in fr:
          for Q in Qs:
            for g in (1,6,12,18,24,30):
              n,d,s1,s2 = design(kind,f,Q,g,fs,wc,qc)
              e = 99 if s1[0] is None or s2[0] is None else errc(n,d,s1,s2,fs,fmax)
              tot+=1
              if e>0.1: fails+=1
              if e>worst[0]: worst=(e,(round(f),Q,g))
        print(fs,kind,wc,qc,fails,'/',tot,round(worst[0],3),worst[1])
