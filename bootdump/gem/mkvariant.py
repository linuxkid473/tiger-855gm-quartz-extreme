# usage: mkvariant.py in.txt out.txt  -- Apple-style depth state: depth BUF_INFO (07800000, addr 0) after the colour BUF_INFO, DST_BUF_VARS 00880308
import sys
B=[];R=[];P=None
for l in open(sys.argv[1]):
    t=l.split()
    if t[0]=='B': B.append(int(t[1],16))
    elif t[0]=='R': R.append([int(t[1]),int(t[2]),int(t[3])])
    elif t[0]=='P': P=l.strip()
i=B.index(0x7d8e0001)               # colour BUF_INFO: cmd, flags/pitch, addr(reloc)
ins=i+3
B[ins:ins]=[0x7d8e0001,0x07800000,0x00000000]
for r in R:
    if r[0]>=ins: r[0]+=3
if len(B)&1: B.append(0)
j=B.index(0x7d850000); B[j+1]=0x00880308
open(sys.argv[2],'w').write(''.join('B %08x\n'%w for w in B)+''.join('R %d %d %d\n'%tuple(r) for r in R)+P+'\n')
print(len(B),R)
