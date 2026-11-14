#!/usr/bin/env python3
"""p53 = p51 with the swap's display wait changed from "pipe B scan-line window" (0x01800020)
to "pipe B vertical blank" (0x01800080) -> tear-free window moves / Dashboard animation.
usage: mk_p53.py AppleIntel830.p51 AppleIntel830.p53
The immediate sits in Intel830Surface::build_swap at vm 0x18a33 (file offset +0x2dc):  05 20 00 80 01  (add eax, 0x01800020)
(eax is 0 there because patch p8 replaced the pipe selection with xor eax,eax)."""
import sys, hashlib
src, dst = sys.argv[1:3]
d = bytearray(open(src, 'rb').read())
o = 0x18a33 + 0x2dc
assert bytes(d[o:o + 5]) == bytes.fromhex('0520008001'), 'not a p51 kext?'
d[o + 1:o + 5] = (0x01800080).to_bytes(4, 'little')
open(dst, 'wb').write(d)
print(dst, hashlib.md5(d).hexdigest())
