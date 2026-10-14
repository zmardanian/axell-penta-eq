import numpy as np
def ls2(f,f0,Q,g):
    A=10**(g/40); s=1j*f/f0; return A*(s*s+np.sqrt(A)/Q*s+A)/(A*s*s+np.sqrt(A)/Q*s+1)
def ls1(f,f0,g):
    T=10**(g/20); s=1j*f/f0; st=np.sqrt(T); return (s+st)/(s+1/st)
f=np.geomspace(10,2000,400)
for kb,qb,ka in ((1.0,0.7,2.5),(1.2,0.8,3.0),(1.0,0.6,3.0),(1.5,0.9,3.5)):
  print('kb',kb,'qb',qb,'ka',ka)
  for b,a in ((10,0),(0,10),(10,10),(10,5),(5,3)):
    H=20*np.log10(np.abs(ls2(f,60*kb,qb,13.5*b/10)*ls1(f,60*ka,-17.5*a/10)))
    i=np.argmin(H[f>60]); fd=f[f>60][i]; mx=np.argmax(H)
    print(f'  b{b} a{a}: @20Hz {H[np.searchsorted(f,20)]:+.1f} @60 {H[np.searchsorted(f,60)]:+.1f} max {H[mx]:+.1f}@{f[mx]:.0f} min {H[f>60][i]:+.1f}@{fd:.0f} ({fd/60:.1f}x)')
