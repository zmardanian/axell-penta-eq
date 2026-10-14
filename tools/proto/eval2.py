from matched import *
def vic_peak(n,d,w0,G):
    a = poles_digital(d); A = Avec(a); p0,p1,p2 = phis(w0)
    B0=A[0]; R1=(A[0]*p0+A[1]*p1+A[2]*p2)*G*G; R2=(-A[0]+A[1]+4*(p0-p1)*A[2])*G*G
    B2=(R1-R2*p1-B0)/(4*p1*p1); B1=R2+B0+4*(p1-p0)*B2
    return factor((B0,B1,B2)),a
def run(kind, method, fs, grid):
    fmax=min(20000,0.95*fs/2); fails=[]; worst=(0,None); tot=0
    for f0,Q,g in grid:
        n,d,w0 = proto(kind,f0,Q,abs(g),fs)
        wm = w0
        if kind=='ls': wm = w0  # midpoint
        if method=='3pt': b,a = three_point(n,d,wm)
        elif method=='vic': b,a = vic_peak(n,d,w0,10**(abs(g)/20))
        else: b,a = ls_fit(n,d,w0)
        e = 99 if b is None else err(n,d,b,a,fs,fmax)
        tot+=1
        if e>0.1: fails.append((round(f0),Q,g,round(e,2)))
        if e>worst[0]: worst=(e,(round(f0),Q,g))
    return len(fails),tot,worst,fails[:6]
fr = np.geomspace(20,20000,31)
for fs in (44100,48000,96000):
  for kind in ('bell','ls','hs'):
    grid=[(f,Q,g) for f in fr for Q in (0.1,0.3,0.5,0.707,1,2,4,8,18) for g in (1,3,6,12,18,24,30)]
    if kind!='bell': grid=[(f,Q,g) for f in fr for Q in (0.3,0.5,0.707,1,1.5,2) for g in (1,3,6,12,18,24,30)]
    for m in (('3pt','vic','ls') if kind=='bell' else ('3pt','ls')):
        print(fs,kind,m,run(kind,m,fs,grid))
