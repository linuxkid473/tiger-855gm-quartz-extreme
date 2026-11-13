#!/usr/bin/env python3
"""p45 = p8 + capture hook: every Intel830::submit_buffer call stores 'this' and the function's own address in a data slot
inside the dead dma_test body (vm 0x13c40..), so a separate test kext (gpustress) can call submit_buffer itself.
Slot block at vm 0x13d00: 'GSM1','GSM2', this, submit_buffer.  usage: mk_p45.py p8 out"""
import sys, hashlib
src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, 'rb').read())
OFF = 732
HOOK, CAVE, DATA, SUBMIT = 0x12157, 0x13c40, 0x13d00, 0x12148
assert d[HOOK+OFF:HOOK+OFF+8].hex() == '85db0f8437030000', d[HOOK+OFF:HOOK+OFF+8].hex()
nxt = CAVE + 5
c = bytearray()
c += bytes.fromhex('e800000000') + b'\x58'                       # call +0 ; pop %eax  (eax = CAVE+5)
c += bytes.fromhex('89b8') + (DATA + 8 - nxt).to_bytes(4, 'little', signed=True)    # mov %edi, slot_this(%eax)
c += bytes.fromhex('8d88') + (SUBMIT - nxt).to_bytes(4, 'little', signed=True)      # lea submit_buffer(%eax),%ecx
c += bytes.fromhex('8988') + (DATA + 12 - nxt).to_bytes(4, 'little', signed=True)   # mov %ecx, slot_fn(%eax)
c += bytes.fromhex('85db')                                                           # test %ebx,%ebx
p = CAVE + len(c); c += bytes.fromhex('0f84') + (0x12496 - (p + 6)).to_bytes(4, 'little', signed=True)   # je bail
p = CAVE + len(c); c += b'\xe9' + (0x1215f - (p + 5)).to_bytes(4, 'little', signed=True)                 # jmp continue
assert CAVE + len(c) <= DATA
d[CAVE+OFF:CAVE+OFF+len(c)] = c
d[DATA+OFF:DATA+OFF+16] = b'GSM1GSM2' + bytes(8)
d[HOOK+OFF:HOOK+OFF+8] = b'\xe9' + (CAVE - (HOOK + 5)).to_bytes(4, 'little', signed=True) + b'\x90\x90\x90'
open(dst, 'wb').write(d)
print('cave', len(c), 'bytes, md5', hashlib.md5(d).hexdigest())
