import numpy as np
from matched import analog, poles_digital, proto
def cospoly_of(a):
    # |A(e^jw)|^2 as polynomial in c=cos w, for real coeffs a (len m+1): sum r_k cos(kw), r autocorr
    m=len(a)-1; r=np.array([np.dot(a[:m+1-k],a[k:]) for k in range(m+1)])
    # cos(kw) -> Chebyshev T_k(c)
    P=np.polynomial.chebyshev.cheb2poly(np.concatenate([[r[0]],2*r[1:]]))
    return P  # ascending powers of c
def design4(n,d,fs,aux_w=0.85,aux_q=0.55,iters=3,ngrid=120):
    a1=poles_digital(d)
    wa=aux_w*np.pi; a2=poles_digital([1/wa**2,1/(aux_q*wa),1])
    a=np.convolve(a1,a2)
    ws=np.concatenate([np.geomspace(2*np.pi*10/fs,np.pi,ngrid)])
    ws=np.concatenate([[0.0],ws])
    c=np.cos(ws)
    Den=np.polyval(cospoly_of(a)[::-1],c)
    T=np.abs(analog(n,d,np.maximum(ws,1e-9)))**2
    y=T*Den
    V=np.stack([c**k for k in range(5)],1)
    wt=1/y
    for it in range(iters):
        coef=np.linalg.lstsq(V*wt[:,None],y*wt,rcond=None)[0]
        fit=V@coef
        wt=1/np.sqrt(np.maximum(fit,1e-300)*y)  # geometric reweight
    # spectral factorization: roots of P(c)
    rc=np.roots(coef[::-1])
    zs=[]
    for ck in rc:
        s=np.sqrt(ck*ck-1+0j); z1=ck+s; z2=ck-s
        zs.append(z1 if abs(z1)<abs(z2) else z2)
    b=np.real(np.poly(zs))
    # scale: match at DC (or w where value is significant)
    g=np.sqrt(np.polyval(coef[::-1],1.0))/abs(np.sum(b)) if abs(np.sum(b))>1e-12 else 1
    if abs(np.sum(b))<1e-9:
        wq=np.pi/2; zq=np.exp(-1j*wq*np.arange(5)); g=np.sqrt(np.polyval(coef[::-1],np.cos(wq)))/abs(np.dot(b,zq))
    return b*g,a
def resp(b,a,w):
    z=np.exp(-1j*np.outer(w,np.arange(len(b)))); za=np.exp(-1j*np.outer(w,np.arange(len(a))))
    return (z@b)/(za@a)
def run(kind,fs,Qs,gains,aux=(0.85,0.55)):
    fr=np.geomspace(20,20000,41); fmax=min(20000,0.95*fs/2)
    f=np.geomspace(20,fmax,800); w=2*np.pi*f/fs
    fails=0;tot=0;worst=(0,None)
    for f0 in fr:
        for Q in Qs:
            for g in gains:
                n,d,w0=proto(kind,f0,Q,g,fs)
                b,a=design4(n,d,fs,*aux)
                e=np.max(np.abs(20*np.log10(np.abs(resp(b,a,w))/np.abs(analog(n,d,w)))))
                tot+=1; fails+= e>0.1
                if e>worst[0]: worst=(e,(round(f0),Q,g))
    return fails,tot,round(worst[0],3),worst[1]
if __name__=='__main__':
    for fs in (44100,48000,96000):
        for aux in ((0.85,0.55),(0.75,0.5),(0.95,0.6)):
            print(fs,aux,'bell',run('bell',fs,(0.1,0.3,0.5,0.707,1,2,4,8,18),(1,6,12,18,24,30),aux))
            print(fs,aux,'ls',run('ls',fs,(0.3,0.5,0.707,1,1.5,2),(1,6,12,18,24,30),aux))
            print(fs,aux,'hs',run('hs',fs,(0.3,0.5,0.707,1,1.5,2),(1,6,12,18,24,30),aux))
