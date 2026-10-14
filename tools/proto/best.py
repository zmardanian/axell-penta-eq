import numpy as np, warnings; warnings.filterwarnings('ignore')
from matched import analog, poles_digital, Avec, phis, factor, digital, proto, three_point, ls_fit
from corr import vic_peak
def err(n,d,b,a,fs,fmax):
    f=np.geomspace(20,fmax,500); w=2*np.pi*f/fs
    return np.max(np.abs(20*np.log10(np.abs(digital(b,a,w))/np.abs(analog(n,d,w)))))
def cands(kind,n,d,w0,g):
    out=[]
    if kind=='bell': out.append(vic_peak(n,d,w0,10**(g/20)))
    out.append(three_point(n,d,w0))
    try: out.append(ls_fit(n,d,w0))
    except Exception: pass
    return [c for c in out if c[0] is not None]
if __name__=="__main__":
  fr=None
for fs in (44100,48000,96000):
    fmax=min(20000,0.95*fs/2)
    for kind in ('bell','ls','hs'):
        Qs=(0.1,0.3,0.5,0.707,1,2,4,8,18) if kind=='bell' else (0.3,0.5,0.707,1,1.5,2)
        res={}
        for f0 in fr:
            for Q in Qs:
                for g in (1,3,6,12,18,24,30):
                    n,d,w0=proto(kind,f0,Q,g,fs)
                    es=[err(n,d,b,a,fs,fmax) for b,a in cands(kind,n,d,w0,g)]
                    res[(f0,Q,g)]=min(es) if es else 99
        def summ(pred):
            v=[e for k,e in res.items() if pred(*k)]; 
            return f"{sum(e>0.1 for e in v)}/{len(v)} worst {max(v):.3f}"
        print(fs,kind,'all:',summ(lambda f,Q,g:True),'| f<=16k Q>=.3 g<=18:',summ(lambda f,Q,g:f<=16001 and Q>=0.3 and g<=18),
              '| f<=10k:',summ(lambda f,Q,g:f<=10001 and Q>=0.3 and g<=18))
