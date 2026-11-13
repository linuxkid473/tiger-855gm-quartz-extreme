#!/usr/bin/env python3
"""Add the three GLD framebuffer entry points that Tiger 10.4.8's GLEngine requires
(_gldCreateFramebuffer, _gldReclaimFramebuffer, _gldDestroyFramebuffer) to the 10.4.1
AppleIntel830GLDriver bundle executable.  Each is an alias of a 3-byte stub
(xor eax,eax ; ret) placed in alignment padding of __text.

usage: glfix.py IN OUT
"""
import struct
import sys

NEW = [b"_gldCreateFramebuffer", b"_gldReclaimFramebuffer", b"_gldDestroyFramebuffer"]


def main(src, dst):
    d = bytearray(open(src, "rb").read())
    ncmds = struct.unpack_from("<I", d, 16)[0]
    p = 28
    lc = {}
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", d, p)
        lc.setdefault(cmd, []).append(p)
        p += size
    symq = lc[2][0]
    dyq = lc[0xb][0]
    symoff, nsyms, stroff, strsize = struct.unpack_from("<IIII", d, symq + 8)
    dy = list(struct.unpack_from("<18I", d, dyq + 8))
    iext, next_, iundef, nundef = dy[2], dy[3], dy[4], dy[5]
    indoff, nind = dy[12], dy[13]
    assert dy[0] == 0 and dy[1] == 0 and iext == 0 and iundef == iext + next_

    # text section and a 3-byte pad to host the stub
    text = None
    for q in lc[1]:
        seg = bytes(d[q + 8:q + 24]).rstrip(b"\0")
        if seg == b"__TEXT":
            nsect = struct.unpack_from("<I", d, q + 48)[0]
            s = q + 56
            for i in range(nsect):
                nm = bytes(d[s:s + 16]).rstrip(b"\0")
                addr, size, off = struct.unpack_from("<III", d, s + 32)
                if nm == b"__text":
                    text = (i + 1, addr, size, off)
                s += 68
    sect_idx, taddr, tsize, toff = text
    # find "c3 90 90 90 <push ebp>" or a run of >=3 nops right after a ret
    stub = None
    for i in range(0, tsize - 4):
        if d[toff + i] == 0xc3 and d[toff + i + 1:toff + i + 4] == b"\x90\x90\x90" and d[toff + i + 4] == 0x55:
            stub = i + 1
            break
    assert stub is not None, "no padding found"
    d[toff + stub:toff + stub + 3] = b"\x31\xc0\xc3"
    stub_addr = taddr + stub
    print("stub at vm 0x%x" % stub_addr)

    syms = []
    for k in range(nsyms):
        syms.append(bytearray(d[symoff + k * 12:symoff + k * 12 + 12]))
    st = bytes(d[stroff:stroff + strsize])
    names = [st[struct.unpack_from("<I", s, 0)[0]:].split(b"\0")[0] for s in syms]

    newst = bytearray(st)
    ext = [(names[k], syms[k]) for k in range(iext, iext + next_)]
    und = [syms[k] for k in range(iundef, iundef + nundef)]
    for nm in NEW:
        off = len(newst)
        newst += nm + b"\0"
        e = bytearray(12)
        struct.pack_into("<IBBhI", e, 0, off, 0x0f, sect_idx, 0, stub_addr)
        ext.append((nm, e))
    ext.sort(key=lambda x: x[0])
    allsyms = [e for _, e in ext] + und
    shift = len(NEW)

    # indirect table: undefined-symbol indices move by +shift
    ind = list(struct.unpack_from("<%dI" % nind, d, indoff))
    ind = [v + shift if (v & 0xc0000000) == 0 and v >= iundef else v for v in ind]

    # rebuild LINKEDIT tail: [locrel ...][symtab][indirect][strtab]
    new_symoff = symoff
    tail = bytearray()
    for e in allsyms:
        tail += e
    new_indoff = new_symoff + len(tail)
    tail += struct.pack("<%dI" % nind, *ind)
    new_stroff = new_symoff + len(tail)
    tail += newst
    assert symoff + nsyms * 12 == indoff and indoff + nind * 4 == stroff, "unexpected layout"
    del d[symoff:]
    d += tail

    struct.pack_into("<IIII", d, symq + 8, new_symoff, len(allsyms), new_stroff, len(newst))
    dy[3] = next_ + shift
    dy[4] = iundef + shift
    dy[12] = new_indoff
    struct.pack_into("<18I", d, dyq + 8, *dy)
    for q in lc[1]:
        seg = bytes(d[q + 8:q + 24]).rstrip(b"\0")
        if seg == b"__LINKEDIT":
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<IIII", d, q + 24)
            nf = len(d) - fileoff
            struct.pack_into("<IIII", d, q + 24, vmaddr, nf, fileoff, nf)  # vmsize, filesize
    open(dst, "wb").write(d)
    print("wrote", dst, len(d))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
