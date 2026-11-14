#!/usr/bin/env python3
"""p55 = p54 without the per-submit wbinvd (the cave at vm 0x13ce0 becomes nop;nop; mov %esi,0x2030(%eax); jmp 0x12389).
A full wbinvd empties both caches on every ring submission (the 2 MB L2 must be refilled afterwards).
usage: mk_p55.py AppleIntel830.p54 AppleIntel830.p55"""
import sys, hashlib
d = bytearray(open(sys.argv[1], 'rb').read())
o = 0x13ce0 + 0x2dc
assert bytes(d[o:o + 2]) == b'\x0f\x09', 'no wbinvd at the p50 cave'
d[o:o + 2] = b'\x90\x90'
open(sys.argv[2], 'wb').write(d); print(sys.argv[2], hashlib.md5(d).hexdigest())
