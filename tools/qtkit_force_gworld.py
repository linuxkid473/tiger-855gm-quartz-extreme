#!/usr/bin/env python3
"""Make QTKit use the QuickDraw ("GWorld") video path instead of Core Image/OpenGL visual contexts.

Why: on a GPU that Core Image cannot use (Intel 855GM), QTKit's QTMovieContentView still picks the VisualContext path when
Quartz Extreme is on; Core Image then runs its fragment programs through the *software* GL renderer's CPU emulator
(PPEmulator*), which costs 80-100% of a Pentium M for 640x360 video.  QTMovieContentView has a "forced imaging mode" flag
word at ivar +0xe8 (bit0 = forced GWorld, bit1 = forced VisualContext).  We make the two getters constant:
    -[QTMovieContentView imagingModeForcedToGWorld]        -> return 1
    -[QTMovieContentView imagingModeForcedToVisualContext] -> return 0
Only the i386 slice of the universal binary is touched (the function bodies are located by byte pattern inside that slice).

usage: qtkit_force_gworld.py QTKit-in QTKit-out        (works on a copy of your own Tiger 10.4.8 QTKit; md5 of the stock file used here: eb4931e6b4db2c7da60318273b26c441)
Install: copy the result over /System/Library/Frameworks/QTKit.framework/Versions/A/QTKit through a NEW inode (cp to a temp name, chown root:wheel, chmod 755, mv -f); keep the original."""
import sys, struct, re, hashlib
src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, 'rb').read())
assert struct.unpack_from('>I', d, 0)[0] == 0xcafebabe, 'not a universal binary'
n = struct.unpack_from('>I', d, 4)[0]
lo = hi = None
for i in range(n):
    cpu, sub, off, size, al = struct.unpack_from('>5I', d, 8 + 20 * i)
    if cpu == 7: lo, hi = off, off + size
assert lo is not None, 'no i386 slice'
body = '5589e58b4508' + '0fb680e8000000' + '24%02x' + '5dc3'   # push ebp; mov ebp,esp; mov eax,[ebp+8]; movzx eax,byte [eax+0xe8]; and al,N; pop ebp; ret
for bit, ret in ((1, 1), (2, 0)):
    pat = bytes.fromhex(body % bit)
    hits = [m.start() for m in re.finditer(re.escape(pat), bytes(d)) if lo <= m.start() < hi]
    assert len(hits) == 1, (bit, hits)
    p = hits[0] + 6
    d[p:p + 9] = bytes.fromhex('b8%02x000000' % ret) + b'\x90' * 4   # mov eax, ret ; nop x4
    print('patched getter bit%d at file offset 0x%x -> returns %d' % (bit, p, ret))
open(dst, 'wb').write(d)
print('md5', hashlib.md5(d).hexdigest())
