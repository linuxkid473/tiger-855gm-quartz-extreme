#!/usr/bin/env python3
"""Make a clang-built i386 MH_OBJECT loadable by Tiger's kld/kextload: drop load commands they don't know
(LC_BUILD_VERSION, LC_VERSION_MIN_*, LC_DATA_IN_CODE, ...), compact the file (shift all file offsets), and make
the segment/section attributes look like gcc's. usage: fixmacho.py in.o out.o"""
import struct, sys
d = bytearray(open(sys.argv[1], 'rb').read())
magic, cpu, sub, ft, ncmds, sizeofcmds, flags = struct.unpack_from('<7I', d, 0)
assert magic == 0xfeedface and ft == 1, "not a 32-bit MH_OBJECT"
KEEP = {0x1, 0x2, 0xb}   # LC_SEGMENT, LC_SYMTAB, LC_DYSYMTAB
off = 28; keep = []
for i in range(ncmds):
    cmd, size = struct.unpack_from('<II', d, off)
    if cmd in KEEP: keep.append(bytearray(d[off:off+size]))
    off += size
newcmds_len = sum(len(c) for c in keep)
delta = sizeofcmds - newcmds_len
def sh(buf, o):                       # shift a non-zero file offset field
    v = struct.unpack_from('<I', buf, o)[0]
    if v: struct.pack_into('<I', buf, o, v - delta)
for c in keep:
    cmd = struct.unpack_from('<I', c, 0)[0]
    if cmd == 1:
        sh(c, 32)                                  # fileoff
        struct.pack_into('<I', c, 44, 3)           # initprot rw
        for k in range(struct.unpack_from('<I', c, 48)[0]):
            so = 56 + 68 * k
            sh(c, so + 40); sh(c, so + 48)          # offset, reloff
            fl = struct.unpack_from('<I', c, so + 64)[0]
            struct.pack_into('<I', c, so + 64, fl & 0xff)
    elif cmd == 2:
        sh(c, 8); sh(c, 16)                         # symoff, stroff
    elif cmd == 0xb:
        for o in (32, 40, 48, 56, 64, 72):          # tocoff modtaboff extrefsymoff indirectsymoff extreloff locreloff
            sh(c, o)
body = bytes(d[28 + sizeofcmds:])
out = bytearray(struct.pack('<7I', magic, cpu, sub, ft, len(keep), newcmds_len, flags)) + b''.join(bytes(c) for c in keep) + body
open(sys.argv[2], 'wb').write(out)
