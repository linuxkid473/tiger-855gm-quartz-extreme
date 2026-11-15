#!/usr/bin/env python3
"""Make glReadPixels(GL_DEPTH_COMPONENT / GL_STENCIL_INDEX) work on the 855GM hardware renderer (see docs/18).

The hardware depth buffer is ONE 32-bit word per pixel: depth in bits 23:0, stencil in bits 31:24.  The DTK driver's
readback stage (vm 0x10dfc) describes it to the user-space converter as a plain UNSIGNED_INT depth buffer (depth read =
word/2^32, stencil read = 0), so depth reads include the stencil bits and are 256x too small and stencil reads return 0.
libGLImage's glgProcessDepth/glgProcessStencil already understand a packed source (format 0xf = depth+stencil, type 0x15 =
"depth in the low 24 bits, stencil in the top byte"); this patch makes the driver describe the surface that way.

Writes two files (the driver also loses the HOME-variant check: hardware GL for every process; GL_REJECT_HW=1 forces software):
  DRIVER_OUT : AppleIntel830GLDriver with the readback fix (input: the b8005166... home-variant driver)
  ENGINE_OUT : GLEngine of the opt-in hardware stack with its driver search path changed from /System/Library/Extensions/
               to /Library/OGL1041/Ext/ (so only processes on /Library/OGL1041 load the patched driver; WindowServer and
               every normal app keep the stock one).  input: the 91ff01fd... GLEngine.
usage: gldriver_depthstencil.py DRIVER_IN DRIVER_OUT ENGINE_IN ENGINE_OUT
Expected: DRIVER_OUT md5 see binaries/apple-derived/AppleIntel830GLDriver.depthstencil, ENGINE_OUT md5 5eb66dcf8d73e023d1c0862069ad550e.
"""
import hashlib, struct, sys
drv_in, drv_out, eng_in, eng_out = sys.argv[1:5]
d = bytearray(open(drv_in, 'rb').read())
assert hashlib.md5(d).hexdigest() == 'b8005166f412b8fcec27a0cbf192e941', "unexpected driver"
def chk(off, hexs):
    b = bytes.fromhex(hexs); assert bytes(d[off:off+len(b)]) == b, (hex(off), bytes(d[off:off+len(b)]).hex())
# depth read: source op = (format 0xf, type 0x15) instead of (0xd, 4)
chk(0x1123d, 'c7458404000000'); d[0x11240] = 0x15
chk(0x11253, 'c745800d000000'); d[0x11256] = 0x0f
# stencil read: the original describes a separate byte plane (format 0xe, type 0) read in kernel mode 5; use the same packed
# 32-bit source and kernel mode 4 (raw copy) as the depth read.  Code cave in __cstring zero padding.
chk(0x112e1, '8d460fc7458400000000')
i = 0x16840; cave = None
while i < 0x18da0 and cave is None:
    if d[i] == 0:
        j = i
        while j < 0x18da0 and d[j] == 0: j += 1
        if j - i >= 50: cave = i + 1
        i = j
    else: i += 1
code = bytes.fromhex('8d4603' 'c7458415000000' '83e0fc' 'c1e002' '89857cffffff' 'c745800f000000' 'c78568ffffff04000000')
code += b'\xe9' + struct.pack('<i', 0x11117 - (cave + len(code) + 5))
d[cave:cave+len(code)] = code
d[0x112e1:0x112e6] = b'\xe9' + struct.pack('<i', cave - (0x112e1 + 5))
# 3. the private copy must give hardware GL to every process, not only to those without HOME: restore the driver's own switch name
#    (getenv("GL_REJECT_HW") - set it to force software for one process) in place of the "HOME" the home-variant uses
assert bytes(d[0x16840:0x16845]) == b'HOME\0' and not any(d[0x16845:0x16850])
d[0x16840:0x16840+13] = b'GL_REJECT_HW\0'
open(drv_out, 'wb').write(d)
e = bytearray(open(eng_in, 'rb').read())
assert hashlib.md5(e).hexdigest() == '91ff01fdd0f7500d810b300ab4e4b1a3', "unexpected GLEngine"
old = b'/System/Library/Extensions/'; p = 0x12fd60
assert bytes(e[p:p+len(old)]) == old
new = b'/Library/OGL1041/Ext/'
e[p:p+len(old)] = new + b'\0' * (len(old) - len(new))
open(eng_out, 'wb').write(e)
print(hashlib.md5(d).hexdigest(), hashlib.md5(e).hexdigest())
