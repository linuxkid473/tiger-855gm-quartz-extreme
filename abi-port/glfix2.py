#!/usr/bin/env python3
"""Make the 10.4.1 AppleIntel830GLDriver fill the capability record that Tiger 10.4.8's GLEngine
passes as the 5th argument of gldCreateContext (arg4 = ctx+0x8a00 record).  10.4.1 had no such
argument, so the record stayed zero (max texture size 0, depth bits 0, ...).

The values are the ones Apple's generic software GLDriver writes (taken from its disassembly), with the
hardware limits of the i830/855 (2048 texture size, 16-bit depth, ARGB8888 colour) substituted.

The fill code lives in unused zero-filled space at the end of the __DATA segment (no NX on this CPU) and
is entered by overwriting the first 6 bytes of gldCreateContext (push ebp; mov eax,0x271e).

usage: glfix2.py IN OUT
"""
import struct
import sys

HOOK = 0x3974          # gldCreateContext entry (vm == file offset in __TEXT)
RET = 0x397a           # original "mov ebp,esp"
CAVE = 0x19800         # inside __DATA, zero-filled
ORIG = bytes.fromhex("55b81e270000")

F1 = 0x3f800000


def dw(off, val):
    if off < 128:
        return b"\xc7\x47" + bytes([off]) + struct.pack("<I", val)
    return b"\xc7\x87" + struct.pack("<I", off) + struct.pack("<I", val)


def wd(off, val):
    if off < 128:
        return b"\x66\xc7\x47" + bytes([off]) + struct.pack("<H", val)
    return b"\x66\xc7\x87" + struct.pack("<I", off) + struct.pack("<H", val)


def by(off, val):
    if off < 128:
        return b"\xc6\x47" + bytes([off]) + bytes([val])
    return b"\xc6\x87" + struct.pack("<I", off) + bytes([val])


def orr(off, val):
    return b"\x81\x8f" + struct.pack("<I", off) + struct.pack("<I", val)


def build():
    c = bytearray()
    c += b"\x55"                      # push ebp           (displaced)
    c += b"\xb8" + struct.pack("<I", 0x271e)  # mov eax,0x271e  (displaced)
    c += b"\x51\x52\x57"              # push ecx; push edx; push edi
    c += b"\x8b\x7c\x24\x24"          # mov edi,[esp+0x24]   ; arg4
    c += b"\x85\xff"                  # test edi,edi
    jz_at = len(c)
    c += b"\x0f\x84\x00\x00\x00\x00"  # jz skip (rel32 patched below)
    fill = bytearray()
    # --- header / viewport ---
    fill += dw(0x00, 4)
    fill += dw(0x04, F1)
    fill += dw(0x08, 3)
    fill += dw(0x0c, 0x800)
    fill += dw(0x10, 0x800)
    fill += dw(0x14, 1)
    fill += dw(0x18, 1)
    fill += wd(0xac, 0) + wd(0xae, 0)
    fill += by(0x24, 1) + by(0x25, 1) + by(0x26, 0)     # double buffered, ...
    # depth / stencil / aux
    fill += dw(0x3c, 16)
    fill += dw(0x40, 0)
    fill += dw(0x54, 0)
    fill += dw(0x58, 0) + dw(0x5c, 0)
    # front colour sizes: total, R,G,B,A
    fill += dw(0x28, 32) + dw(0x2c, 8) + dw(0x30, 8) + dw(0x34, 8) + dw(0x38, 8)
    fill += dw(0x44, 8) + dw(0x48, 8) + dw(0x4c, 8) + dw(0x50, 8)
    # point/line/offset constants
    fill += dw(0x60, 0x3dcccccd) + dw(0x64, 0x42480000) + dw(0x68, 0x38d1b717)
    fill += dw(0x6c, 0x3dcccccd) + dw(0x70, 0x41200000) + dw(0x74, 0x3dcccccd)
    fill += by(0x78, 0) + by(0x79, 0) + by(0x7a, 0) + by(0x7b, 0)
    for o in (0x7c, 0x7e, 0x80, 0x82, 0x84, 0x86):
        fill += wd(o, 0)
    fill += dw(0x88, 0x3e8) + dw(0x8c, 0) + dw(0x90, 0) + dw(0x94, 3) + dw(0x98, 0) + dw(0x9c, 0)
    fill += dw(0xa0, 0x80) + dw(0xa4, 0x80) + dw(0xb0, F1)
    fill += wd(0xb8, 8) + wd(0xba, 8) + wd(0xbc, 0) + wd(0xbe, 8)
    fill += wd(0xc0, 0x800)             # max texture size (i830: 2048)
    fill += wd(0xc2, 0) + wd(0xc4, 0x800) + wd(0xc6, 0)   # 3D, rectangle (2048), cube
    fill += dw(0xec, 0)
    fill += by(0xca, 0) + by(0xcb, 0)
    fill += orr(0x138, 0xf00002b1) + orr(0x13c, 0x80007) + orr(0x140, 0x20000)
    c += fill
    skip = len(c)
    struct.pack_into("<i", c, jz_at + 2, skip - (jz_at + 6))
    c += b"\x5f\x5a\x59"              # pop edi; pop edx; pop ecx
    jmp_at = len(c)
    c += b"\xe9" + struct.pack("<i", RET - (CAVE + jmp_at + 5))
    return bytes(c)


def main(src, dst):
    d = bytearray(open(src, "rb").read())
    assert d[HOOK:HOOK + 6] == ORIG, d[HOOK:HOOK + 6].hex()
    cave = build()
    assert all(b == 0 for b in d[CAVE:CAVE + len(cave) + 16]), "cave area not empty"
    assert CAVE + len(cave) < 0x19ff0
    d[CAVE:CAVE + len(cave)] = cave
    d[HOOK:HOOK + 6] = b"\xe9" + struct.pack("<i", CAVE - (HOOK + 5)) + b"\x90"
    open(dst, "wb").write(d)
    print("cave %d bytes at 0x%x; wrote %s" % (len(cave), CAVE, dst))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
