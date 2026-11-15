#!/usr/bin/env python3
"""MPlayer OSX Extended rev 11 on Tiger: its binaries link /System/Library/Frameworks/CoreVideo.framework (10.5+ only).
Tiger keeps the CoreVideo symbols inside QuartzCore, so rewrite the install name (same length budget, NUL padded).
usage: patch_mplayer_corevideo.py FILE [FILE...]   (writes FILE.cvpatch next to each; install over the original)
Files in the app: Contents/MacOS/MPlayer OSX Extended and Contents/Resources/External_Binaries/mplayer.app/Contents/MacOS/mplayer"""
import sys
old = b"/System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo"
new = b"/System/Library/Frameworks/QuartzCore.framework/QuartzCore"
new += b"\0" * (len(old) - len(new))
for f in sys.argv[1:]:
    d = open(f, 'rb').read()
    n = d.count(old)
    open(f + '.cvpatch', 'wb').write(d.replace(old, new))
    print(f, 'occurrences replaced:', n)
