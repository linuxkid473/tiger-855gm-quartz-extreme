#!/usr/bin/env python3
"""p51 = p50 + one-time 3D engine PRIMING at the first ring submission after the kext starts.

Finding (see docs): after a power-up the 855GM's 3D engine hangs on Apple's first compositor batches until a complete, self-contained
state batch (the Linux-proven IGT gen2 textured copy) has been executed once; the primed state then survives warm reboots.  Nothing
in Apple's start-up path does this on our retail-10.4.8 port.  p51 emits that batch inline into the ring (render target / texture =
GTT windows 0x02800000 / 0x02804000, which are scratch-mapped at that point by the p46 fill) right before the first job.

Hook: vm 0x12298 (the 3 BB_START dwords of submit_buffer) -> jmp ENTRY.  Code and the 72-dword batch (as 'push imm32' sequences,
pushed in reverse so they sit in ascending order on the stack) are spread over the free gaps between relocations in the dead
Intel830GLContext::dma_test body (vm 0x13d10 ..), computed from the kext's own relocation table.  file offset = vm + 732.
usage: mk_p51.py p50-in p51-out
"""
import sys, struct, subprocess, hashlib
src, dst = sys.argv[1], sys.argv[2]
OFF = 732
d = bytearray(open(src, 'rb').read())
HOOK, HOOK_END = 0x12298, 0x122bb
BATCH = [0x7c500000, 0x7c281088, 0x7d8c0000, 0x10000000, 0x7c510000, 0x7c291099, 0x7d8c0000, 0x30000000, 0x7c520000, 0x7c2a10aa, 0x7d8c0000, 0x50000000, 0x7c530000, 0x7c2b10bb, 0x7d8c0000, 0x70000000, 0x7d020000, 0x0000ba98, 0x7c800002, 0x7d8b0000, 0xa0000000, 0x7d8d0001, 0x00000008, 0x3f800000, 0x6ba00000, 0x7d880000, 0x00000000, 0x682008a1, 0x63a00aae, 0x6423060e, 0x6b800000, 0x63a00aaa, 0x7d030081, 0x82000180, 0x02000180, 0x7d8e0001, 0x03000100, 0x02800000, 0x7d850000, 0x00880300, 0x7d800003, 0x00000000, 0x00000000, 0x003f003f, 0x00000000, 0x7d030804, 0x02804000, 0x07e0fcc0, 0x07e00000, 0x00000000, 0x00000000, 0x7c08c0cc, 0x7d0410c2, 0x00001000, 0x000020c0, 0x00000004, 0x6a000000, 0x7f1c000b, 0x42800000, 0x42800000, 0x3f800000, 0x3f800000, 0x00000000, 0x42800000, 0x00000000, 0x3f800000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x02000001, 0x00000000]
assert len(BATCH) == 72
orig = bytes(d[HOOK + OFF:HOOK_END + OFF]); assert len(orig) == 35 and orig[:7].hex() == 'c70416800080' + '18'

# gaps between relocations in vm 0x13d10..0x14090 (dead code), from the kext's own relocation table
out = subprocess.run(['xcrun', 'otool', '-arch', 'i386', '-rv', src], capture_output=True, text=True).stdout
rel = sorted(int(l.split()[0], 16) for l in out.splitlines() if l.split() and len(l.split()[0]) == 8 and 0x13cf0 <= int(l.split()[0], 16) < 0x14090)
gaps = []; s = 0x13d10
for a in rel:
    if a > s and a - s >= 8: gaps.append([s, a - s])
    s = max(s, a + 4)
FLAG = gaps[0][0]; gaps[0][0] += 4; gaps[0][1] -= 4          # flag byte at the start of the first gap
def take(n, prefer=None):
    for g in gaps:
        if g[1] >= n: a = g[0]; g[0] += n; g[1] -= n; return a
    raise SystemExit('out of gap space for %d bytes' % n)

d[FLAG + OFF] = 0                                            # the flag byte must start cleared (dead code leaves garbage)
def place(addr, blob):
    d[addr + OFF:addr + OFF + len(blob)] = blob

# --- blocks (addresses assigned first, then encoded so relative jumps are right)
A_ENTRY = take(31); A_ORIG = take(40); A_FINAL = take(49)
# push fragments: each holds k pushes + jmp(5); data pushed in reverse so dword0 ends up at the lowest stack address
rev = list(reversed(BATCH)); frags = []; i = 0
while i < len(rev):
    best = None
    for g in gaps:
        k = (g[1] - 5) // 5
        if k >= 1 and (best is None or g[1] > best[1]): best = g
    if best is None: raise SystemExit('no space for push fragments')
    k = min((best[1] - 5) // 5, len(rev) - i)
    a = best[0]; best[0] += 5 * k + 5; best[1] -= 5 * k + 5
    frags.append((a, rev[i:i + k])); i += k
# encode
def jmp(frm, to): return b'\xe9' + struct.pack('<i', to - (frm + 5))
for n, (a, vals) in enumerate(frags):
    blob = b''.join(b'\x68' + struct.pack('<I', v) for v in vals)
    nxt = frags[n + 1][0] if n + 1 < len(frags) else A_FINAL
    place(a, blob + jmp(a + len(blob), nxt))
p0 = frags[0][0]
# ENTRY: pic base; if flag set -> ORIG; set flag; push data
e = bytearray()
e += b'\xe8\x00\x00\x00\x00' + b'\x58'                                    # call +0 ; pop %eax   (eax = A_ENTRY+5)
pic = A_ENTRY + 5
e += b'\x80\xb8' + struct.pack('<i', FLAG - pic) + b'\x00'                  # cmpb $0, FLAG(%eax)
e += b'\x0f\x85' + struct.pack('<i', A_ORIG - (A_ENTRY + len(e) + 6))         # jne ORIG
e += b'\xc6\x80' + struct.pack('<i', FLAG - pic) + b'\x01'                  # movb $1, FLAG(%eax)
e += jmp(A_ENTRY + len(e), p0)
assert len(e) <= 31, len(e)
place(A_ENTRY, bytes(e))
# ORIG: the stock BB_START emission, then back
place(A_ORIG, orig + jmp(A_ORIG + len(orig), HOOK_END))
# FINAL: copy 288 bytes from the stack into the ring, drop them, account for the ring space, continue with ORIG
f = bytearray()
f += b'\x51'                                                                   # push %ecx
f += b'\x8d\x8c\x24' + struct.pack('<i', 4 + 288)                            # lea 292(%esp),%ecx   end
f += b'\x8d\x5c\x24\x04'                                                     # lea 4(%esp),%ebx     src
L = len(f)
f += b'\x8b\x03' + b'\x89\x04\x16' + b'\x83\xc3\x04' + b'\x83\xc6\x04' + b'\x39\xcb'  # mov (%ebx),%eax; mov %eax,(%esi,%edx); add 4,ebx; add 4,esi; cmp ecx,ebx
f += b'\x72' + bytes([(L - (len(f) + 2)) & 0xff])                              # jb loop
f += b'\x59'                                                                   # pop %ecx
f += b'\x81\xc4' + struct.pack('<I', 288)                                      # add $288,%esp
f += b'\x81\xaf\xec\x05\x00\x00' + struct.pack('<I', 288)                    # sub $288,0x5ec(%edi)   ring free space
f += jmp(A_FINAL + len(f), A_ORIG)
assert len(f) <= 49, len(f)
place(A_FINAL, bytes(f))
# hook
d[HOOK + OFF:HOOK_END + OFF] = (b'\xe9' + struct.pack('<i', A_ENTRY - (HOOK + 5))).ljust(35, b'\x90')
open(dst, 'wb').write(d)
print('md5', hashlib.md5(d).hexdigest(), 'fragments', len(frags), 'flag at vm 0x%x' % FLAG)
