#!/usr/bin/env python3
"""p46 = input kext + 'fill the whole graphics page table with the scratch page' at the end of the scratch-page setup in
Intel830::start_graphics_engine (hook vm 0x10d73, replaces 'lea 0x5dc(%esi),%edx').  Apple's commit/releaseAGPMemory already map
released entries to the scratch page; unused entries were never initialised (stay invalid from BIOS) -> GPU faults/stalls when a batch
touches them.  Linux i915 fills the whole table with a scratch page.  Fills entries 1024.. (above the 4 MB BIOS stolen mapping) up to
table end.  Cave at vm 0x13c80 (dead dma_test body; capture-hook cave of mk_p45 lives at 0x13c40..0x13cc0).  usage: mk_p46.py in out"""
import sys, hashlib
src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, 'rb').read())
OFF = 732
HOOK, CAVE, BACK = 0x10d73, 0x13c80, 0x10d79
assert d[HOOK+OFF:HOOK+OFF+6].hex() == '8d96dc050000', d[HOOK+OFF:HOOK+OFF+6].hex()
c = bytearray()
c += bytes.fromhex('8b9674050000')   # mov 0x574(%esi),%edx   GTT base (MMIO+0x10000)
c += bytes.fromhex('8b86d4050000')   # mov 0x5d4(%esi),%eax   scratch page phys
c += bytes.fromhex('83c801')         # or $1,%eax
c += bytes.fromhex('8b8e5c050000')   # mov 0x55c(%esi),%ecx   table bytes
c += bytes.fromhex('c1e902')         # shr $2,%ecx            entries
c += bytes.fromhex('bb00040000')     # mov $0x400,%ebx        first entry
loop = len(c)
c += bytes.fromhex('39cb')           # cmp %ecx,%ebx
c += bytes.fromhex('7306')           # jae done (skips 6)
c += bytes.fromhex('89049a')         # mov %eax,(%edx,%ebx,4)
c += bytes.fromhex('43')             # inc %ebx
c += bytes.fromhex('eb') + bytes([(loop - (len(c) + 2)) & 0xff])   # jmp loop
c += bytes.fromhex('8d96dc050000')   # lea 0x5dc(%esi),%edx   (the instruction we replaced)
p = CAVE + len(c)
c += b'\xe9' + (BACK - (p + 5)).to_bytes(4, 'little', signed=True)
assert CAVE + len(c) < 0x13cf7
d[CAVE+OFF:CAVE+OFF+len(c)] = c
d[HOOK+OFF:HOOK+OFF+6] = b'\xe9' + (CAVE - (HOOK + 5)).to_bytes(4, 'little', signed=True) + b'\x90'
open(dst, 'wb').write(d)
print('cave', len(c), 'md5', hashlib.md5(d).hexdigest())
