#!/usr/bin/env python3
"""p56: replace the per-submit wbinvd (whole-cache flush, p50/p51) by a clflush of the 4 KB ring (64 lines).

Intel830::submit_buffer ends with: tail(esi) -> 0x5f0(%edi); [wbinvd]; mov %esi,0x2030(%eax) (PRB0_TAIL); jmp 0x12389.
edi = Intel830 object; the ring is 4 KB (0x5e8 = 0x1000, mask 0x5f4 = 0xfff), ring VA = [0x5fc] + [0x598].
wbinvd empties both caches (the 2 MB L2 has to be refilled afterwards); flushing just the 64 ring lines costs microseconds.
Code lives in three reloc-free pockets (all dead code / padding):
  R0 @0x12370 (25 B; was the 'tail < size-0x40' sanity check that only logged, + the jmp to the old wbinvd cave)
       eax = ring VA ; edx = 0 ; ecx = 64 ; jmp A
  A  @0x13ce0 (22 B; the old p50 cave)      L: clflush (eax,edx) ; edx += 64 ; loop L ; jmp B
  B  @0x1229d (padding)                     eax = regs base ; sfence ; mov %esi,0x2030(%eax) ; jmp 0x12389
Input: a p5x kext that has the p50 cave at 0x13ce0 (p51..p55f).   usage: mk_p56.py IN OUT"""
import sys, struct, hashlib
src, dst = sys.argv[1:3]
d = bytearray(open(src, 'rb').read())
B = 0x2dc
R0, A, BB, CONT = 0x12370, 0x13ce0, 0x1229d, 0x12389
def chk(vm, hexs):
    o = vm + B; assert bytes(d[o:o + len(hexs)//2]) == bytes.fromhex(hexs), (hex(vm), bytes(d[o:o+len(hexs)//2]).hex())
chk(R0, '8b87e8050000' '83e840' '39c6' '7320' '8b8770050000' 'e9')
assert bytes(d[A+B:A+B+2]) in (b'\x0f\x09', b'\x90\x90'), 'unexpected cave'
chk(A+2, '89b030200000' 'e9')
assert all(x in (0x90, 0xcc, 0) for x in d[BB + B:BB + B + 30]), 'pocket B is not padding'
def rel(frm_end, to): return struct.pack('<i', to - frm_end)
def put(vm, code, room):
    assert len(code) <= room, (hex(vm), len(code), room)
    for i in range(room): d[vm + B + i] = 0x90
    d[vm + B:vm + B + len(code)] = code
r0 = bytes.fromhex('8b87fc050000' '038798050000' '31d2' '6a40' '59'); r0 += b'\xe9' + rel(R0 + len(r0) + 5, A)
a = bytes.fromhex('0fae3c10' '83c240' 'e2f7'); a += b'\xe9' + rel(A + len(a) + 5, BB)
b = bytes.fromhex('8b8770050000' '0faef8' '89b030200000'); b += b'\xe9' + rel(BB + len(b) + 5, CONT)
put(R0, r0, 25); put(A, a, 22); put(BB, b, 30)
open(dst, 'wb').write(d); print(dst, hashlib.md5(d).hexdigest())
