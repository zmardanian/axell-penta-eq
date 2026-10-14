from matched import *
fs=44100
for (kind,f0,Q,g) in [('bell',6325,0.707,-24),('bell',1000,1,6),('bell',10000,2,12),('ls',1000,0.707,6),('ls',11247,0.5,-24)]:
    n,d,w0 = proto(kind,f0,Q,g,fs)
    a = poles_digital(d); A = Avec(a)
    H2 = lambda w: abs(analog(n,d,w))**2
    B0 = H2(1e-9)*A[0]; B1 = H2(np.pi)*A[1]; p0,p1,p2 = phis(w0)
    B2 = (H2(w0)*magsq_from(A,w0) - B0*p0 - B1*p1)/p2
    ws = np.linspace(0,np.pi,2000); ms = magsq_from((B0,B1,B2),ws)
    print(kind,f0,Q,g,'a',a,'B',B0,B1,B2,'min magsq',ms.min(), 'roots', np.roots(d))
