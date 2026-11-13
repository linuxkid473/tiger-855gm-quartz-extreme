#!/usr/bin/env python3
"""p47 = p46 + Linux chip register values (FW_BLC 0x20d8=011f0108, FW_BLC2 0x20dc=102, MI_ARB_STATE 0x20e4=2, RENCLK_GATE_D1 0x6204=1) written at the same hook,
because a COLD boot leaves the BIOS values and the GPU stalls at stamp 2 (warm reboots keep whatever Linux/earlier sessions left).
p46 = input kext + 'fill the whole graphics page table with the scratch page' at the end of the scratch-page setup in
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
c += bytes.fromhex('8b8e70050000')                                   # mov 0x570(%esi),%ecx   MMIO base
c += bytes.fromhex('c78104620000') + bytes.fromhex('01000000')      # movl $1,0x6204(%ecx)   RENCLK_GATE_D1
c += bytes.fromhex('c781d8200000') + bytes.fromhex('08011f01')       # movl $0x011f0108,0x20d8(%ecx)  FW_BLC
c += bytes.fromhex('c781dc200000') + bytes.fromhex('02010000')       # movl $0x102,0x20dc(%ecx)       FW_BLC2
c += bytes.fromhex('c781e4200000') + bytes.fromhex('02000000')       # movl $2,0x20e4(%ecx)           MI_ARB_STATE
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
assert CAVE + len(c) < 0x13cf7, hex(CAVE+len(c))
d[CAVE+OFF:CAVE+OFF+len(c)] = c
d[HOOK+OFF:HOOK+OFF+6] = b'\xe9' + (CAVE - (HOOK + 5)).to_bytes(4, 'little', signed=True) + b'\x90'
open(dst, 'wb').write(d)
print('cave', len(c), 'md5', hashlib.md5(d).hexdigest())
