#!/usr/bin/env python3
"""Grow an (unmounted, cleanly journaled) HFS+ volume in place.
usage: hfsgrow.py DEVICE NEW_TOTAL_BLOCKS EXT_START [--yes]
 - DEVICE must already be (at least) NEW_TOTAL_BLOCKS*blocksize bytes; the volume must end exactly at NEW_TOTAL_BLOCKS*bs
   (partition bytes == total*bs, so the alternate volume header sits in the second-last sector, as Apple's tools do)
 - the allocation file (bitmap) is extended by new extent(s) starting at EXT_START (a block in the NEW free area)
 - old last block (held the alternate VH) is freed, new last block is marked used, free count is adjusted
Only touches: allocation bitmap blocks, primary VH, alternate VH. Aborts if the journal is not clean or the volume was not unmounted."""
import struct, sys, os, time

dev, new_total, ext_start = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
YES = '--yes' in sys.argv
BS = 4096
f = open(dev, 'r+b' if YES else 'rb')
f.seek(0, 2); devsize = f.tell()
if YES:
    assert devsize == new_total * BS, 'device size %d != volume size %d (partition must end exactly at the volume end)' % (devsize, new_total * BS)

def rd(off, n):
    f.seek(off); return f.read(n)

vh = bytearray(rd(1024, 512))
assert vh[:2] == b'H+', 'not HFS+'
attrs, = struct.unpack('>I', vh[4:8])
assert attrs & 0x100, 'volume not cleanly unmounted'
assert not attrs & 0x800, 'volume flagged inconsistent'
assert struct.unpack('>I', vh[40:44])[0] == BS
old_total, old_free = struct.unpack('>II', vh[44:52])
assert new_total > old_total
# journal clean?
jib, = struct.unpack('>I', vh[12:16])
if jib:
    ji = rd(jib * BS, 52)
    joff, = struct.unpack('>Q', ji[36:44])
    jh = rd(joff, 44)
    endian, = struct.unpack('>I', jh[4:8])
    e = '>' if endian == 0x12345678 else '<'
    magic, endianv, start, end = struct.unpack(e + 'IIQQ', jh[:24])
    assert start == end, 'journal not clean (start %d end %d)' % (start, end)
    print('journal clean (start == end)')

ALLOC = 112
lsz, clump, ftot = struct.unpack('>QII', vh[ALLOC:ALLOC + 16])
ext = [list(struct.unpack('>II', vh[ALLOC + 16 + 8 * i:ALLOC + 24 + 8 * i])) for i in range(8)]
assert sum(c for s, c in ext) == ftot, 'allocation file has overflow extents; unsupported'
need_bytes = (new_total + 7) // 8
need_blocks = (need_bytes + BS - 1) // BS
extra = max(0, need_blocks - ftot)
print('old total', old_total, 'new total', new_total, 'bitmap blocks', ftot, '->', need_blocks, '(+%d)' % extra, 'ext_start', ext_start)
if extra:
    assert old_total <= ext_start and ext_start + extra < new_total - 1, 'ext range outside new free area'
# load bitmap through its extents
bm = bytearray()
for s, c in ext:
    if c: bm += rd(s * BS, c * BS)
assert len(bm) == ftot * BS
newbm = bytearray(bm) + bytearray(extra * BS)
def getbit(b): return (newbm[b >> 3] >> (7 - (b & 7))) & 1
def setbit(b, v):
    m = 0x80 >> (b & 7)
    if v: newbm[b >> 3] |= m
    else: newbm[b >> 3] &= ~m & 0xff
# sanity of the old state
assert getbit(old_total - 1) == 1, 'old last block is not marked used'
used_old = sum(bin(x).count('1') for x in bm[:old_total >> 3])
assert (old_total & 7) == 0 and old_total - used_old == old_free, 'free count mismatch: bitmap says %d, VH says %d' % (old_total - used_old, old_free)
assert not any(bm[old_total >> 3:]), 'bits beyond old end are not clear'
# apply
setbit(old_total - 1, 0)
for b in range(ext_start, ext_start + extra): setbit(b, 1)
setbit(new_total - 1, 1)
# extent bookkeeping
if extra:
    last = max(i for i, (s, c) in enumerate(ext) if c)
    if ext[last][0] + ext[last][1] == ext_start:
        ext[last][1] += extra
    else:
        assert last < 7, 'no free extent slot'
        ext[last + 1] = [ext_start, extra]
used_new = sum(bin(x).count('1') for x in newbm[:new_total >> 3]) if (new_total & 7) == 0 else None
new_free = old_free + (new_total - old_total) - extra
if used_new is not None:
    assert new_total - used_new == new_free, 'internal: free %d vs %d' % (new_total - used_new, new_free)
print('free blocks', old_free, '->', new_free)
nvh = bytearray(vh)
struct.pack_into('>II', nvh, 44, new_total, new_free)
struct.pack_into('>QII', nvh, ALLOC, need_blocks * BS, clump, need_blocks)
for i, (s, c) in enumerate(ext): struct.pack_into('>II', nvh, ALLOC + 16 + 8 * i, s, c)
wc, = struct.unpack('>I', nvh[68:72]); struct.pack_into('>I', nvh, 68, (wc + 1) & 0xffffffff)
print('new allocation extents:', [(s, c) for s, c in ext if c])
if not YES:
    print('DRY RUN: nothing written (pass --yes)'); sys.exit(0)
# write: bitmap (through the extents), alt VH (new), clear old alt VH, then primary VH last
pos = 0
for s, c in ext:
    if c:
        f.seek(s * BS); f.write(newbm[pos:pos + c * BS]); pos += c * BS
f.flush(); os.fsync(f.fileno())
f.seek(old_total * BS - 1024); f.write(bytes(512))          # wipe old alternate header
f.seek(new_total * BS - 1024); f.write(nvh)                  # new alternate header
f.flush(); os.fsync(f.fileno())
f.seek(1024); f.write(nvh)                                   # primary header last
f.flush(); os.fsync(f.fileno())
print('WRITTEN. total', new_total, 'free', new_free)
