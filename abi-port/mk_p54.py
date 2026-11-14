#!/usr/bin/env python3
"""p54 = p53 + low-latency GPU waits.

Problem: Intel830::waitForTimeStamp (the wait behind glFinishObjectAPPLE / blit-idle / finish) polled the
GPU stamp with IOSleep(1) per iteration.  On Tiger/x86 a 1 ms sleep takes ~10.4 ms (100 Hz timer), so every
CPU<->GPU wait that is not already complete costs >=10 ms.  WindowServer (texture finish + cursor unshield
blit-idle) spent ~45% of its time in those waits while moving windows.

Fix: poll with thread_block(0) (a yield; _thread_block is already imported by the kext) instead of
IOSleep(1), and scale the iteration limit so the overall time-out stays in the seconds range.

Edits (vm = file offset - 0x2dc):
  0x11f4d  movl $1,(%esp)        c7 04 24 01 00 00 00  ->  arg 0
  reloc @0x11f55  _IOSleep (sym 528) -> _thread_block (sym 961)
  0x11f72  cmpl $0x3e7,%ecx      -> cmpl $(LIMIT-2),%ecx
  0x11fb8  cmpl $0x3e9,%esi      -> cmpl $LIMIT,%esi
usage: mk_p54.py AppleIntel830.p53 AppleIntel830.p54 [LIMIT]"""
import sys, struct, hashlib
src, dst = sys.argv[1:3]
LIMIT = int(sys.argv[3]) if len(sys.argv) > 3 else 2000000
d = bytearray(open(src, 'rb').read())
B = 0x2dc
def need(vm, hexs):
    o = vm + B
    assert bytes(d[o:o + len(hexs) // 2]) == bytes.fromhex(hexs), (hex(vm), bytes(d[o:o + len(hexs) // 2]).hex())
    return o
o = need(0x11f4d, 'c70424' + '01000000'); d[o + 3:o + 7] = (0).to_bytes(4, 'little')
o = need(0x11f72, '81f9e7030000'); d[o + 2:o + 6] = (LIMIT - 2).to_bytes(4, 'little')
o = need(0x11fb8, '81fee9030000'); d[o + 2:o + 6] = (LIMIT).to_bytes(4, 'little')
# relocation entry for the call operand at 0x11f55: find __text reloc table
nc = struct.unpack_from('<I', d, 16)[0]; off = 28; reloff = nreloc = None
for _ in range(nc):
    c, sz = struct.unpack_from('<II', d, off)
    if c == 1:
        so = off + 56
        for i in range(struct.unpack_from('<I', d, off + 48)[0]):
            if d[so:so + 16].rstrip(b'\0') == b'__text':
                reloff, nreloc = struct.unpack_from('<II', d, so + 48)
            so += 68
    off += sz
symoff = nsyms = stroff = None
off = 28
for _ in range(nc):
    c, sz = struct.unpack_from('<II', d, off)
    if c == 2: symoff, nsyms, stroff, strsz = struct.unpack_from('<IIII', d, off + 8)
    off += sz
def symidx(name):
    for i in range(nsyms):
        s = struct.unpack_from('<I', d, symoff + 12 * i)[0]
        e = d.index(b'\0', stroff + s)
        if d[stroff + s:e].decode() == name: return i
    raise SystemExit('symbol %s not found' % name)
sleep_i, block_i = symidx('_IOSleep'), symidx('_thread_block')
done = False
for i in range(nreloc):
    a, packed = struct.unpack_from('<II', d, reloff + 8 * i)
    if a == 0x11f55 and not a & 0x80000000:
        assert packed & 0xffffff == sleep_i, 'unexpected reloc symbol'
        struct.pack_into('<II', d, reloff + 8 * i, a, (packed & ~0xffffff) | block_i); done = True
assert done, 'reloc for 0x11f55 not found'
open(dst, 'wb').write(d)
print(dst, hashlib.md5(d).hexdigest(), 'limit', LIMIT)
