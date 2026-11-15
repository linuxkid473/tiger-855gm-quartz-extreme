#!/usr/bin/env python3
"""Replace unconditional SSE3 `movddup xmm, m64` by an SSE2 equivalent so PowerVLC runs on a Pentium M.
site: F2 0F 12 /r  ->  E9 <cave> + NOPs ;  cave: F2 0F 10 /r (movsd) ; 66 0F 14 modrm (unpcklpd xmm,xmm) ; E9 <back>
usage: patch_movddup.py in.dylib out.dylib"""
import sys, struct
src, dst = sys.argv[1:3]
d = bytearray(open(src, 'rb').read())
CAVE_BASE = 0x10fe00         # tail of __eh_frame (0x10f9b8..0x110000), unwinding data a C program never reads; file offset == vm for __TEXT
CAVE_END = 0x10ffe0
assert 0x10f9b8 <= CAVE_BASE < CAVE_END <= 0x110000
sites = {0x1cb3f: 'f20f12742 43c'.replace(' ', ''), 0x74214: 'f20f124424 40'.replace(' ', ''),
         0xb410b: 'f20f1289 30010000'.replace(' ', ''), 0xbc011: 'f20f125424 08'.replace(' ', '')}
cave = CAVE_BASE
for va, hexs in sites.items():
    orig = bytes.fromhex(hexs)
    assert bytes(d[va:va + len(orig)]) == orig, (hex(va), bytes(d[va:va + len(orig)]).hex())
    modrm = orig[3]
    reg = (modrm >> 3) & 7
    mov = bytes([0xf2, 0x0f, 0x10]) + orig[3:]                      # movsd xmm, m64 (same modrm/sib/disp)
    unp = bytes([0x66, 0x0f, 0x14, 0xc0 | (reg << 3) | reg])        # unpcklpd xmm, xmm
    body = mov + unp
    back = va + len(orig)
    jmp_back = b'\xe9' + struct.pack('<i', back - (cave + len(body) + 5))
    blob = body + jmp_back
    assert cave + len(blob) <= CAVE_END
    d[cave:cave + len(blob)] = blob
    d[va:va + len(orig)] = b'\xe9' + struct.pack('<i', cave - (va + 5)) + b'\x90' * (len(orig) - 5)
    print('site %#x -> cave %#x (xmm%d, %d bytes)' % (va, cave, reg, len(blob)))
    cave += (len(blob) + 3) & ~3
open(dst, 'wb').write(d)
