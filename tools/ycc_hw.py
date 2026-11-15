#!/usr/bin/env python3
"""Hardware YCbCr video for the Intel 855GM (Tiger 10.4.x, DTK 10.4.1 GL stack).

Two small patches (both verified on a Dell Inspiron 510m with VLC 0.9.10):

1. GLDriver (AppleIntel830GLDriver, 10.4.1 DTK build, 111004 bytes)
   The driver's per-format texture table (9 dwords per record) already has
   GL_APPLE_ycbcr_422 records that use the 855GM's native 4:2:2 map format
   (MAPSURF_422 = 0x140) with the sampler colour-space-conversion bit (0x4).
   VLC uploads type 0x85ba (8_8), whose record had NORMAL (0x14c); it needs SWAPY (0x144):
       record YCBCR/8_8_REV (type 0x85bb) @0x191f8 : 0x144 (SWAPY)  (unchanged)
       record YCBCR/8_8     (type 0x85ba) @0x1921c : 0x14c (NORMAL) -> 0x144 (SWAPY)
   Both records end up SWAPY.  (An earlier version of this script *swapped* the two records; that fixed
   VLC but broke WindowServer's own video-surface path, which uses 8_8_REV: radioactive colours.)

2. GLEngine (10.4.1 copy, i386 slice at file offset 4096)
   The hardware renderer reports none of the 73 optional extension bits, so it
   does not advertise texture_rectangle / bgra / ycbcr_422 and VLC refuses to
   use it.  glGetString(GL_EXTENSIONS) always prints a fixed 25-string list from a
   pointer table at vm 0x12f340; three rarely used entries
   (NV_texgen_reflection, NV_light_max_exponent, IBM_rasterpos_clip) are retargeted:
       slot 21 -> "GL_EXT_texture_rectangle GL_ARB_texture_rectangle" (0x12d420)
       slot 22 -> "GL_EXT_bgra"                                      (0x12d520)
       slot 23 -> "GL_APPLE_ycbcr_422"                                (0x12d700)

usage: ycc_hw.py driver_in driver_out engine_in engine_out
"""
import struct, sys

def patch_driver(src, dst):
    d = bytearray(open(src, 'rb').read())
    assert len(d) == 111004, 'unexpected driver size'
    rev, std = struct.unpack('<II', bytes(d[0x191f8:0x191fc]) + bytes(d[0x1921c:0x1921c + 4]))
    if rev == 0x144 and std in (0x14c, 0x144):
        struct.pack_into('<I', d, 0x1921c, 0x144)
    elif (rev, std) == (0x14c, 0x144):       # output of the old swapping version: put 8_8_REV back
        struct.pack_into('<I', d, 0x191f8, 0x144)
    else:
        raise SystemExit('driver table not as expected: %x %x' % (rev, std))
    open(dst, 'wb').write(d)

def patch_engine(src, dst):
    d = bytearray(open(src, 'rb').read())
    base = 4096
    tab = 0x12f340
    want = {21: 0x12d420, 22: 0x12d520, 23: 0x12d700}
    for i, a in want.items():
        e = d.find(b'\0', base + a)
        print(i, hex(a), d[base + a:e].decode('latin1'))
        struct.pack_into('<I', d, base + tab + 4 * i, a)
    open(dst, 'wb').write(d)

if __name__ == '__main__':
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    patch_driver(sys.argv[1], sys.argv[2])
    patch_engine(sys.argv[3], sys.argv[4])
