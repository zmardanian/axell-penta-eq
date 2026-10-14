import numpy as np, warnings; warnings.filterwarnings('ignore')
from matched import analog, poles_digital, digital, proto
from best import err
def mzt(n,d):
    a=poles_digital(d)
    nz=np.roots(n) if abs(n[0])>0 else np.roots(n[1:])
    b=np.real(np.poly(np.exp(nz)))
    if len(b)<3: b=np.concatenate([b,[0]*(3-len(b))])
    g=abs(analog(n,d,1e-9))/abs(digital(b,a,1e-9))
    return b*g,a
fr=np.geomspace(20,20000,41)
for fs in (44100,48000,96000):
    fmax=min(20000,0.95*fs/2)
    for kind in ('bell','ls','hs'):
        Qs=(0.3,0.5,0.707,1,2,4,8) if kind=='bell' else (0.3,0.5,0.707,1,1.5,2)
        bad=[];tot=0
        for f0 in fr:
            for Q in Qs:
                for g in (1,6,12,18):
                    n,d,w0=proto(kind,f0,Q,g,fs); b,a=mzt(n,d)
                    e=err(n,d,b,a,fs,fmax); tot+=1
                    if e>0.1: bad.append((round(f0),Q,g,round(e,2)))
        print(fs,kind,len(bad),'/',tot,'lowest failing f0',min([x[0] for x in bad]) if bad else None)
