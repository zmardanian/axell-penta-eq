from eval2 import *
import itertools
fr = np.geomspace(20,20000,41)
for fs in (44100,48000,96000):
  for kind in ('bell','ls','hs'):
    for fmaxband in (12000,16000,20000):
      grid=[(f,Q,g) for f in fr if f<=fmaxband for Q in ((0.3,0.5,0.707,1,2,4,8,18) if kind=='bell' else (0.5,0.707,1,1.5)) for g in (1,3,6,12,18)]
      for m in (('vic','3pt') if kind=='bell' else ('3pt','ls')):
        nf,tot,worst,fails = run(kind,m,fs,grid)
        print(fs,kind,'f<=',fmaxband,m,nf,'/',tot,'worst',round(worst[0],3),worst[1],fails[:4])
