# p55/p56 (cache flush), video freeze, VLC, Wi-Fi start-up

## p55 / p56: no more whole-cache flush per GPU submission
* `p55` = p54 with the p50 `wbinvd` turned into `nop; nop` (`mk_p55.py`); `p55f` = p55 with the swap's display wait removed (the `0x01800080` of p53 becomes `0`, MI_NOOP): fast mode, tearing returns but the software cursor flickers less (the cursor is hidden by WindowServer until the GPU is idle again, so a vblank-synced swap keeps it hidden longer).
* `p56` / `p56f` (`mk_p56.py`) put a *precise* flush back: the 4 KB ring (`0x5e8 = 0x1000`, mask `0x5f4`, ring VA = `[0x5fc]+[0x598]`) is flushed with 64 `clflush` + `sfence` before the tail write (PRB0_TAIL, 0x2030). The code lives in three reloc-free pockets (the old sanity-check block at vm 0x12370, the old p50 cave at 0x13ce0, nop padding at 0x1229d). User verdict: "way snappier".
* `tools/system/gfx-mode vsync|fast|status` swaps the kext (and rebuilds the mkext). Remember the kext cache pitfall (docs/09).

## Videos that freeze the machine (640x384)
Two H.264 clips (640x384, Baseline) hard-froze the Dell within ~3 s of pressing play in QuickTime (ssh/ping dead, CPU idle in the last sample, a burst of ~1000 GPU jobs just before). Same on p51 (no p53-p56 changes) and with the original GA plug-in (all capability queries unsupported), so it is not the new kext work and not the accelerated-window-surface path. 640x360 / 640x426 clips of the same files play fine, so the trigger is the 384-line frame size (page-multiple surface size?). Not root-caused; workaround: scale to 640x360.

Encode like the hardware likes (Pentium M 1.6 GHz, QuickTime 7.1.3 / Apple H.264): **Constrained Baseline, 1 ref, no B-frames, CAVLC, native frame rate** (never resample 30 -> 24: it drops frames unevenly and looks choppy):
```
ffmpeg -i in.mov -vf scale=640:360:flags=lanczos,setsar=1 -c:v libx264 -profile:v baseline -level 3.0 -preset slow -crf 20 \
  -maxrate 2800k -bufsize 3000k -x264-params ref=1:bframes=0:cabac=0:weightp=0 -c:a copy -movflags +faststart out.mov
```
Main profile with B-frames made the same video choppy.

## VLC 0.9.10 (Intel)
All its video outputs are OpenGL (`macosx`, `opengl`, `minimal_macosx`). `-[VLCGLView initWithVout:]` asks for `NSOpenGLPFAAccelerated`; with software-only GL that fails ("could not create OpenGL video output", sound but black). Patch (file offset 0x50cd2 of `libmacosx_plugin.dylib`): `c7 45 a4 49 00 00 00` -> `... 48 ...` (Accelerated -> NoRecovery). Then it plays, but at ~1 fps: 85% of the video thread is in the software renderer's fragment-emulation path (`gldFPEmulateScanline`, the YCbCr 4:2:2 rectangle texture) - needs hardware GL (ycbcr) to be fast. Use QuickTime.

## Wi-Fi start-up (tools/wifi)
* crash guard counts consecutive unfinished starts (skip only after two), StopService clears it;
* `iwi-connect boot` never gives up (retries), logs why when not linked (every 30 s), restarts the card (`cmd=5` then `cmd=9`) after 90 s and every 120 s;
* link watchdog in the sync loop re-issues `cmd=9` when the link is down for ~15 s;
* StartupItem no longer requires "Network" (`Requires = Disks`, `OrderPreference = Early`) so association starts earlier; publishing to configd waits until `scutil` answers.
