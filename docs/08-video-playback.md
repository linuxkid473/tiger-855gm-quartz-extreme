# Video playback under Quartz Extreme

With QE running, QuickTime Player 7 (QTKit) was blank, then very slow. This page records why, and the fix.

## Symptom 1 - blank/white video plane

The hardware renderer exposes 25 GL extensions instead of the software renderer's 45; it lacks `GL_APPLE_ycbcr_422`, `GL_EXT/ARB_texture_rectangle` and `GL_EXT_bgra`, which QuickTime's CoreVideo path needs, and the GA plug-in answers every capability query with "unsupported" (so `cgls`/`BGRA` surface creation fails with `CGXBindSurface: Invalid surface`). `abi-port/gafix.py` makes the plug-in answer those two queries.

Workaround that keeps QE for the compositor: the GL driver has a debug switch - at the start of `gldChoosePixelFormat`, if `getenv("GL_REJECT_HW")` is non-NULL it returns no hardware pixel format. Patching the *name* `GL_REJECT_HW` to `HOME` makes every normal application (which has `HOME` in its environment) fall back to the software renderer, while WindowServer (whose environment has only `PATH`) keeps the hardware QE compositor. Patch applied to the pristine DTK driver at file offset 0x16840 (see `abi-port/glfix*.py` notes).

## Symptom 2 - QuickTime uses 80-100% CPU at 640x360

`sample` on QuickTime Player during playback:

```
-[QTMovieContentView drawRect:fromDisplayLink:]
  glEnd_Exec -> gleRenderSmoothQuadsFunc -> gldRenderPolygonPtr -> gldTessellate... -> gldFPEmulateScanline
    PPEmulatorRun -> PPEmulatorRunInterpreter -> PPEmulatorEmulateOp / PPTextureSamplerSampleTexelRECT ...
```

The software GL renderer interprets Core Image's fragment programs on the CPU for every pixel.

### What did *not* help

* Hiding the fragment-program / fragment-shader / ATI-text-fragment-shader / YCbCr extension strings in `GLEngine` (`tools/mangle.pl`, `hide_ati.pl`, `ycchide.pl`): a normal GL context reports them hidden (`tools/ext.rb`), yet QuickTime still ends up in the emulator.
* `CI_NO_ACCEL=1` in the environment.
* Removing the per-submit `wbinvd` from the kext (p52): the GPU/kernel side was not the bottleneck (WindowServer used 1-7% CPU).

## The fix - force QTKit's QuickDraw ("GWorld") path

Disassembling QTKit (i386 slice) shows `QTMovieContentView` chooses between a QuickDraw GWorld path (`MoviesTask`, what QE-off mode used) and a *visual context* path (`QTVisualContextCopyImageForTime` -> Core Image -> OpenGL). There is a flag word at ivar `+0xe8` with `bit0 = forced GWorld`, `bit1 = forced visual context`, read by two tiny getters:

```
-[QTMovieContentView imagingModeForcedToGWorld]         : movzbl 0xe8(%eax),%eax ; andb $1,%al
-[QTMovieContentView imagingModeForcedToVisualContext]  : movzbl 0xe8(%eax),%eax ; andb $2,%al
```

`tools/qtkit_force_gworld.py` replaces the 9-byte body of each (`0f b6 80 e8 00 00 00 24 0N` -> `b8 0N 00 00 00 90 90 90 90`) so the first returns 1 and the second 0. Only the i386 slice is touched.

Result on the Inspiron 510m: QuickTime Player CPU 80-100% -> ~30%, the machine ~60% idle, the emulator disappears from the profile, QE stays on.

Install (needs your own copy of the stock QTKit; stock md5 used here `eb4931e6b4db2c7da60318273b26c441`, patched `32fdc01f2bbd75eab151b543066ef0c2`):

```bash
python3 tools/qtkit_force_gworld.py QTKit.stock QTKit.gworld
# on the Mac, keep a backup, then replace via a NEW inode
sudo cp -p /System/Library/Frameworks/QTKit.framework/Versions/A/QTKit /Library/QTKit.orig
sudo cp QTKit.gworld /System/Library/Frameworks/QTKit.framework/Versions/A/QTKit.new
sudo chown root:wheel .../QTKit.new && sudo chmod 755 .../QTKit.new && sudo mv -f .../QTKit.new .../QTKit
```

Side effect: every QTKit-based app uses the QuickDraw path (no Core Image video effects). Undo = put `QTKit.orig` back.

## Remaining

* Making the *hardware* renderer usable for video would need `GL_APPLE_ycbcr_422`, texture rectangles and BGRA in the hardware path (see [07-open-issues.md](07-open-issues.md)).
* Core Image itself stays unsupported (no fragment-program hardware on the 855GM).
* VLC: see [15](15-hardware-ycbcr-video.md) (hardware YCbCr path, ~27% CPU).
