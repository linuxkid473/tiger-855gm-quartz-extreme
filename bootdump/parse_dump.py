#!/usr/bin/env python3
"""Parse a bootdump region (32 sectors) read from the stick into readable text.
usage: dd if=/dev/rdiskN bs=512 skip=1228800 count=32 of=dump.bin ; parse_dump.py dump.bin [dump2.bin]"""
import struct, sys

def parse(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'GDMP', "no dump header (not written?)"
    pci, mmio = {}, {}
    o = 8
    while o + 4 <= len(d):
        t = d[o:o+4]
        if t == b'PCI ':
            ident = struct.unpack_from('<I', d, o+4)[0]
            pci[ident] = struct.unpack_from('<64I', d, o+8); o += 264
        elif t == b'MMIO':
            st, c = struct.unpack_from('<II', d, o+4)
            for i, v in enumerate(struct.unpack_from('<%dI' % c, d, o+12)):
                mmio[st + 4*i] = v
            o += 12 + 4*c
        else:
            break
    return pci, mmio

def show(pci, mmio):
    for ident, w in sorted(pci.items()):
        print("PCI %02x:%02x.%x  (ven:dev %04x:%04x)" % ((ident >> 16) & 0xff, (ident >> 11) & 0x1f, (ident >> 8) & 7, w[0] & 0xffff, w[0] >> 16))
        for i in range(0, 64, 4):
            print("  %02x: %08x %08x %08x %08x" % (i*4, *w[i:i+4]))
    print("MMIO (nonzero registers)")
    for a in sorted(mmio):
        if mmio[a]:
            print("  %06x: %08x" % (a, mmio[a]))

if __name__ == '__main__':
    if len(sys.argv) == 2:
        show(*parse(sys.argv[1]))
    else:
        (p1, m1), (p2, m2) = parse(sys.argv[1]), parse(sys.argv[2])
        print("PCI differences (%s -> %s)" % (sys.argv[1], sys.argv[2]))
        for k in sorted(set(p1) | set(p2)):
            a, b = p1.get(k, [0]*64), p2.get(k, [0]*64)
            for i in range(64):
                if a[i] != b[i]:
                    print("  %02x:%02x.%x +%02x: %08x -> %08x" % ((k >> 16) & 0xff, (k >> 11) & 0x1f, (k >> 8) & 7, i*4, a[i], b[i]))
        print("MMIO differences")
        for a in sorted(set(m1) | set(m2)):
            if m1.get(a, 0) != m2.get(a, 0):
                print("  %06x: %08x -> %08x" % (a, m1.get(a, 0), m2.get(a, 0)))
