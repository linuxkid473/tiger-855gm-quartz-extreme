#!/usr/bin/env python3
"""gafix.py: make the 10.4.1 AppleIntel830GA plug-in answer the capability queries Tiger 10.4.8's WindowServer/CoreGraphics makes.

The real 10.4.8 GA plug-ins (ATIRadeon8500GA ...) implement _GetCapabilities for the 4CCs 'cgls' and 'BGRA' by storing kCFBooleanTrue/False in
*out and returning 0 ('slvm' asks the kernel).  The 10.4.1 Intel plug-in returns kIOReturnUnsupported (0xe00002c7) for everything, so
hardware-accelerated window surfaces (QuickTime video, any GL window with an accelerated renderer) are refused.
 - repoint the plug-in's ___sF non-lazy pointer at CoreFoundation's kCFBooleanTrue (symbol rename in the string table, library ordinal copied from _CFEqual)
 - new _GetCapabilities in the zero padding at the end of __TEXT: 'cgls' / 'BGRA' -> *out = kCFBooleanTrue, 0; else 0xe00002c7
 - interface table slot (vm 0x3064, has a local relocation) -> new function
usage: gafix.py AppleIntel830GA out [bgra]   ('bgra' = answer only the BGRA query; 'cgls' stays unsupported)"""
import sys, struct, hashlib
src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, 'rb').read())
# header
magic, cputype, cpusub, ftype, ncmds, sizeofcmds, flags = struct.unpack_from('<7I', d, 0)
assert magic == 0xfeedface and cputype == 7
off = 28; symtab = None; linkedit = None
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', d, off)
    if cmd == 2: symtab = off
    if cmd == 1 and d[off+8:off+8+16].rstrip(b'\0') == b'__LINKEDIT': linkedit = off
    off += size
symoff, nsyms, stroff, strsize = struct.unpack_from('<4I', d, symtab + 8)
def sym(i):
    n_strx, n_type, n_sect, n_desc, n_value = struct.unpack_from('<IBBHI', d, symoff + 12 * i)
    name = bytes(d[stroff + n_strx: d.index(b'\0', stroff + n_strx)]).decode()
    return n_strx, n_type, n_sect, n_desc, n_value, name
idx = {sym(i)[5]: i for i in range(nsyms)}
i_sF, i_eq = idx['___sF'], idx['_CFEqual']
cf_ord = (sym(i_eq)[3] >> 8) & 0xff
n = sym(i_sF)
print('___sF sym', i_sF, 'desc %04x' % n[3], ' CFEqual ordinal', cf_ord)
newname = b'_kCFBooleanTrue\0'
assert stroff + strsize == len(d)
newstrx = strsize
d += newname
struct.pack_into('<I', d, symtab + 20, strsize + len(newname))                 # strsize
struct.pack_into('<IBBHI', d, symoff + 12 * i_sF, newstrx, n[1], n[2], (cf_ord << 8) | (n[3] & 0xff), n[4])
# __LINKEDIT filesize
fsz = struct.unpack_from('<I', d, linkedit + 36)[0]
struct.pack_into('<I', d, linkedit + 36, fsz + len(newname))
vsz = struct.unpack_from('<I', d, linkedit + 28)[0]
if vsz < fsz + len(newname):
    struct.pack_into('<I', d, linkedit + 28, (fsz + len(newname) + 0xfff) & ~0xfff)
# new function: single chunk in the unused tail of the __DATA page (0x3170..0x3fff is zero, referenced by nothing; i386 Pentium-M has no NX).
# NOTE: the zero runs at the end of __TEXT are NOT free - they sit inside __cstring and hold (empty) string slots the code points at.
CAVE = 0x3200
assert d[CAVE:CAVE + 0x80] == bytes(0x80), 'cave not free'
c = bytearray()
c += bytes.fromhex('8b542408')                                  # mov 8(%esp),%edx      selector
c += bytes.fromhex('81fa736c6763') + b'\x74\x00'                # cmp $'cgls',%edx ; je yes   (rel8 patched)
j1 = len(c) - 1
c += bytes.fromhex('81fa41524742') + b'\x74\x00'                # cmp $'BGRA',%edx ; je yes
j2 = len(c) - 1
c += bytes.fromhex('b8c70200e0c3')                              # mov $0xe00002c7,%eax ; ret
yes = len(c)
c += bytes.fromhex('e800000000') + b'\x58'                      # call +0 ; pop %eax   (eax = CAVE + yes + 5)
c += bytes.fromhex('8b80') + (0x3168 - (CAVE + yes + 5)).to_bytes(4, 'little', signed=True)   # mov slot(%eax),%eax = &kCFBooleanTrue
c += bytes.fromhex('8b00')                                      # mov (%eax),%eax = kCFBooleanTrue
c += bytes.fromhex('8b54240c')                                  # mov 0xc(%esp),%edx    out
c += bytes.fromhex('8902')                                      # mov %eax,(%edx)
c += bytes.fromhex('31c0c3')                                    # xor %eax,%eax ; ret
c[j1] = (yes - (j1 + 1)) & 0xff
c[j2] = (yes - (j2 + 1)) & 0xff
d[CAVE:CAVE + len(c)] = c
CODE = CAVE
if not (len(sys.argv) > 3 and sys.argv[3] == 'novtable'):
    struct.pack_into('<I', d, 0x3064, CODE)
if len(sys.argv) > 3 and sys.argv[3] == 'bgra':
    k = d.index(bytes.fromhex('81fa736c6763'), 0x3200)
    d[k + 2:k + 6] = bytes(4)          # first compare can never match -> 'cgls' falls through to unsupported
open(dst, 'wb').write(d)
print('new GetCapabilities at 0x%x, %d bytes; md5 %s' % (CODE, len(c), hashlib.md5(d).hexdigest()))
