#!/usr/bin/env python3
"""p44 = p37 + inline batch copy.

Stock Intel830::submit_buffer puts 'MI_BATCH_BUFFER_START <gtt addr>' in the ring, so the GPU later reads the batch from
memory that Apple's driver reuses and patches in place (the race we suspect behind the random 3D stall).  p44 instead copies
the batch's n dwords straight into the ring (the GPU never reads shared batch memory) plus one MI_NOOP so the tail stays
qword aligned.  Batches larger than 0x3fe dwords fall back to the stock BB_START.  If the ring lacks room the code jumps to
the stock wait loop (refreshRing + IOSleep) and retries.

Hook: vm 0x12298..0x122bb (the 3 BB_START dwords) -> jmp cave.  Cave: dead dma_test body at vm 0x13c40 (no relocations up to
0x13cf6).  file offset = vm + 732.   usage: mk_p44.py in-kext-binary out-kext-binary
"""
import sys, hashlib

src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, 'rb').read())
OFF = 732
HOOK, HOOK_END, CAVE, WAIT = 0x12298, 0x122bb, 0x13c40, 0x12461
CAVE_LIMIT = 0x13cf7

orig = bytes(d[HOOK + OFF:HOOK_END + OFF])
assert len(orig) == 35 and orig[:7].hex() == 'c70416800080' + '18', orig.hex()

c = bytearray()
fixups = []  # (position of rel32 in c, offset just past the instruction, label)
labels = {}


def jcc(op, label):
    c.extend(op)
    fixups.append((len(c), len(c) + 4, label))
    c.extend(b'\0\0\0\0')


c += bytes.fromhex('8b4514')                 # mov 0x14(%ebp),%eax        n
c += bytes.fromhex('3dfe030000')             # cmp $0x3fe,%eax
jcc(bytes.fromhex('0f87'), 'fallback')       # ja fallback
c += bytes.fromhex('8d048580000000')         # lea 0x80(,%eax,4),%eax     bytes needed
c += bytes.fromhex('3987ec050000')           # cmp %eax,0x5ec(%edi)       free space
jcc(bytes.fromhex('0f8c'), 'wait')           # jl stock wait loop
c += bytes.fromhex('8b5d0c')                 # mov 0xc(%ebp),%ebx         src
c += bytes.fromhex('8b4514')                 # mov 0x14(%ebp),%eax
c += bytes.fromhex('8945e0')                 # mov %eax,-0x20(%ebp)       counter
labels['loop'] = len(c)
c += bytes.fromhex('8b03')                   # mov (%ebx),%eax
c += bytes.fromhex('890416')                 # mov %eax,(%esi,%edx)
c += bytes.fromhex('83c304')                 # add $4,%ebx
c += bytes.fromhex('83c604')                 # add $4,%esi
c += bytes.fromhex('21ce')                   # and %ecx,%esi
c += bytes.fromhex('ff4de0')                 # decl -0x20(%ebp)
c += bytes.fromhex('75') + bytes([0])        # jnz loop
c[-1] = (labels['loop'] - len(c)) & 0xff
c += bytes.fromhex('c70416') + bytes(4)      # movl $0,(%esi,%edx)        MI_NOOP pad
c += bytes.fromhex('83c604')                 # add $4,%esi
c += bytes.fromhex('21ce')                   # and %ecx,%esi
c += bytes.fromhex('8b4514')                 # mov 0x14(%ebp),%eax
c += bytes.fromhex('8d0485f8ffffff')         # lea -8(,%eax,4),%eax       (n+1)*4 - 12
c += bytes.fromhex('2987ec050000')           # sub %eax,0x5ec(%edi)
jcc(bytes.fromhex('e9'), 'back')             # jmp 0x122bb
labels['fallback'] = len(c)
c += orig                                    # stock BB_START emission
jcc(bytes.fromhex('e9'), 'back')

end = CAVE + len(c)
assert end <= CAVE_LIMIT, hex(end)
tgt = {'wait': WAIT, 'back': HOOK_END}
for pos, past, label in fixups:
    t = tgt[label] if label in tgt else CAVE + labels[label]
    c[pos:pos + 4] = (t - (CAVE + past)).to_bytes(4, 'little', signed=True)

d[CAVE + OFF:CAVE + OFF + len(c)] = c
hook = bytearray(b'\xe9' + (CAVE - (HOOK + 5)).to_bytes(4, 'little', signed=True))
hook += b'\x90' * (35 - len(hook))
d[HOOK + OFF:HOOK_END + OFF] = hook
open(dst, 'wb').write(d)
print('cave bytes', len(c), 'ends', hex(end), 'md5', hashlib.md5(d).hexdigest())
