import struct, sys
f = open(sys.argv[1], 'rb'); BS = 4096
def rd(o, n): f.seek(o); return f.read(n)
vh = rd(1024, 512); tot, free = struct.unpack('>II', vh[44:52])
f.seek(0, 2); size = f.tell()
alt = rd(tot * BS - 1024, 512)
print('total', tot, 'free', free, 'device blocks', size // BS, 'exact fit', size == tot * BS)
print('alt VH == primary:', alt == vh, '(sig %s)' % alt[:2])
lsz, cl, ftot = struct.unpack('>QII', vh[112:128]); ext = [struct.unpack('>II', vh[128 + 8 * i:136 + 8 * i]) for i in range(8)]
print('alloc fork logical', lsz, 'blocks', ftot, 'extents', [e for e in ext if e[1]])
bm = b''.join(rd(s * BS, c * BS) for s, c in ext if c)
bits = lambda b: (bm[b >> 3] >> (7 - (b & 7))) & 1
used = sum(bin(x).count('1') for x in bm[:tot >> 3])
print('bitmap used', used, 'free', tot - used, 'matches VH free:', tot - used == free)
print('bitmap tail zero beyond volume:', not any(bm[tot >> 3:]), ' last block used:', bits(tot - 1), ' old last (5081599) used:', bits(5081599))
print('ext blocks all marked used:', all(bits(b) for s, c in ext if c for b in range(s, s + c)))
print('metadata blocks 0..3 used:', [bits(b) for b in range(4)])
