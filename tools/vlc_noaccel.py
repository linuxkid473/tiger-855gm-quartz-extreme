#!/usr/bin/env python3
"""VLC 0.9.10 (Intel) libmacosx_plugin.dylib: stop requiring a hardware-accelerated OpenGL pixel format.
-[VLCGLView initWithVout:] builds {DoubleBuffer, Accelerated(0x49), NoRecovery(0x48), ...}; with software-only GL
NSOpenGLPixelFormat fails ("could not create OpenGL video output": sound, black picture).  Patch: 0x49 -> 0x48.
usage: vlc_noaccel.py IN OUT"""
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
sig = bytes.fromhex('c745a449000000'); assert d.count(sig) == 1, 'unexpected plug-in'
d[d.find(sig) + 3] = 0x48
open(sys.argv[2], 'wb').write(d)
