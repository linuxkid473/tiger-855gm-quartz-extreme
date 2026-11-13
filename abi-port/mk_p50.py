#!/usr/bin/env python3
"""p50 = p48 + two small patches (the actual boot-time Quartz Extreme fix is the second one):

 1. READ_FLUSH on the post-batch ring flush: file 0x1259a (vm 0x122be) 00 -> 01, i.e. MI_FLUSH 02000000 -> 02000001
    (MI_FLUSH | READ_FLUSH).  Found by replaying Apple's stalled batch on Linux (see docs).  Result 'p49'.
 2. wbinvd before the ring TAIL write in Intel830::submit_buffer.  The ported kext writes ring + batch memory through cacheable
    CPU mappings, so the GPU can fetch stale RAM -> random stalls (worst at boot).  Hook vm 0x12383 ('movl %esi,0x2030(%eax)')
    -> jmp cave at vm 0x13ce0: wbinvd; movl %esi,0x2030(%eax); jmp 0x12389.   (dead dma_test body, no relocations up to 0x13cf6)

file offset = vm + 732.   usage: mk_p50.py p48-in p50-out
"""
import sys, struct, hashlib
src, dst = sys.argv[1], sys.argv[2]
NOCACHE = len(sys.argv) > 3 and sys.argv[3] == 'nocache'   # skip the wbinvd hook (p51 priming made it unnecessary; it costs a lot of CPU)
d = bytearray(open(src, 'rb').read())
OFF = 732
# 1. READ_FLUSH
o = 0x1259a
assert d[o - 3:o + 4].hex() == 'c70416' + '00000002', d[o - 3:o + 4].hex()
d[o] = 0x01
# 2. cache flush before tail update
HOOK, CAVE, BACK = 0x12383, 0x13ce0, 0x12389
if NOCACHE:
    open(dst, 'wb').write(d); print('md5 (READ_FLUSH only)', hashlib.md5(d).hexdigest()); sys.exit(0)
assert d[HOOK + OFF:HOOK + OFF + 6].hex() == '89b030200000', d[HOOK + OFF:HOOK + OFF + 6].hex()
cave = bytes.fromhex('0f09') + bytes.fromhex('89b030200000') + b'\xe9' + struct.pack('<i', BACK - (CAVE + 13))
d[CAVE + OFF:CAVE + OFF + len(cave)] = cave
d[HOOK + OFF:HOOK + OFF + 6] = b'\xe9' + struct.pack('<i', CAVE - (HOOK + 5)) + b'\x90'
open(dst, 'wb').write(d)
print('md5', hashlib.md5(d).hexdigest())
