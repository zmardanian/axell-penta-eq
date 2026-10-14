import numpy as np, warnings; warnings.filterwarnings('ignore')
from matched import analog, digital, proto, three_point, factor
from corr import vic_peak
def minphase_fir(logmag_half, L):
    # logmag_half: ln|R| on N/2+1 bins (0..pi); returns min-phase FIR of length L
    N=(len(logmag_half)-1)*2
    full=np.concatenate([logmag_half, logmag_half[-2:0:-1]])
    c=np.fft.ifft(full).real
    fold=np.zeros(N); fold[0]=c[0]; fold[1:N//2]=2*c[1:N//2]; fold[N//2]=c[N//2]
    h=np.fft.ifft(np.exp(np.fft.fft(fold))).real
    w=np.ones(L); t=L//4; w[L-t:]=0.5*(1+np.cos(np.pi*np.arange(t)/t))  # half-Hann taper at tail
    return h[:L]*w
def design_band(kind,f0,Q,g,fs):
    n,d,w0=proto(kind,f0,Q,abs(g),fs)
    if kind=='bell': b,a=vic_peak(n,d,w0,10**(abs(g)/20))
    else: b,a=three_point(n,d,w0)
    if g<0:  # reciprocal
        b,a = a/b[0], b/b[0]
        n,d = d,n
    return n,d,b,a
def evaluate(kind,f0,Q,g,fs,L,N=4096):
    n,d,b,a=design_band(kind,f0,Q,g,fs)
    if b is None: return 99,99
    wg=np.linspace(0,np.pi,N//2+1); wg[0]=1e-9
    R=np.log(np.abs(analog(n,d,wg))/np.abs(digital(b,a,wg)))
    h=minphase_fir(R,L)
    f=np.geomspace(20,min(20000,0.95*fs/2),600); w=2*np.pi*f/fs
    Hc=np.array([np.sum(h*np.exp(-1j*wi*np.arange(L))) for wi in w])
    e0=np.max(np.abs(20*np.log10(np.abs(digital(b,a,w))/np.abs(analog(n,d,w)))))
    e1=np.max(np.abs(20*np.log10(np.abs(digital(b,a,w)*Hc)/np.abs(analog(n,d,w)))))
    return e0,e1
if __name__=='__main__':
    fr=np.geomspace(20,20000,25)
    for fs,L in ((44100,256),(48000,256),(96000,512)):
        for kind in ('bell','ls','hs'):
            Qs=(0.1,0.3,0.707,2,8,18) if kind=='bell' else (0.3,0.707,1.5,2)
            r=[]
            for f0 in fr:
                for Q in Qs:
                    for g in (-30,-12,6,18,30):
                        e0,e1=evaluate(kind,f0,Q,g,fs,L); r.append((e1,e0,round(f0),Q,g))
            r.sort(reverse=True)
            print(fs,kind,'fails',sum(x[0]>0.1 for x in r),'/',len(r),'worst',[tuple(np.round(x[:2],3))+x[2:] for x in r[:4]])
