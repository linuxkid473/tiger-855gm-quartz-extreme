# Video players and the GPU: what works, what locks the machine up (2026-11-15)

## Corrections to docs/15
* The first shipped YCbCr table patch *swapped* the two records (8_8 <-> 8_8_REV). That made VLC's colours right but broke WindowServer's own video-surface path (it uses 8_8_REV): radioactive colours in any app drawing video through a window surface. The correct patch sets **both** records to SWAPY (`0x144`): `tools/ycc_hw.py` and `binaries/apple-derived/AppleIntel830GLDriver.home-variant.ycc` (md5 `b8005166f412b8fcec27a0cbf192e941`) now do that. (`b8005166...` was already the installed file; the repo copy and docs were stale.)
* "p54t" (docs/16) was installed as the stable kext, but it did not cure the video-window freezes below: **p51 froze the same way**, so those freezes are not caused by the p53/p54/p56 patches.

## Players tried
| Player | Result |
|---|---|
| QuickTime Player 7 (QTKit patched onto the QuickDraw path) | **works**: correct colours, ~30% CPU at 640x360 Baseline H.264, no freezes. The recommended player. |
| VLC 0.9.10 normal (software GL) | works, ~1 fps (fragment-program emulation) |
| VLC 0.9.10 via `vlc-hw` (hardware GL) | fast (~27% CPU, 0 late pictures) but can hang the GPU after minutes (docs/16) |
| **PowerVLC 2.1.0** (Tiger/Intel build) | crashes at launch: unconditional SSE3 code (`movddup` x4 in libpowervlccore, hundreds more in libavcodec). The Pentium M has no SSE3. The `semthex` 8.8.1 kernel has a half-finished SSE3 hook in the commpage whose trap, `int $0xfd`, is a kernel-only IDT gate (access byte `0x8E`; user gates are `0x80-0x82` and `0xfb`, which is the CPUID-spoof handler), so the process dies with a GPF. Patching the 4 library sites (SSE2 `movsd`+`unpcklpd` in a code cave in `__eh_frame`) gets it to launch but it still dies on the first video (400+ more sites). A kernel with a working SSE3 emulator would be the real fix. |
| **MPlayer OSX Extended rev 11** (June 2009, last Tiger revision; Internet Archive copy of the googlecode file) | runs after a one-line loader patch (the binaries link `/System/Library/Frameworks/CoreVideo.framework`, which only exists on 10.5+; on Tiger the CoreVideo symbols live in QuartzCore, so the install name is rewritten to `/System/Library/Frameworks/QuartzCore.framework/QuartzCore`). Decoding with no window is fast (300 frames of 640x360 H.264 in 1.2 s). The fontconfig library crashes (NULL deref) unless `-nofontconfig`. **Any window output (`quartz`, `corevideo`) shows a radioactive/green window and then locks the machine up instantly**, on p54t, p51 and with either YCbCr table. |

## Conclusion
Every path that puts video on screen through the GPU's YCbCr handling (hardware-GL textures, QuickTime's visual-context path, MPlayer's window outputs) has frozen the 855GM at least once; only the CPU path (QuickTime on QuickDraw) is reliable. The DTK GL driver was never a shipping product (retail Tiger has only the GMA 950 driver), so there is no "real" driver to fall back on.
Not done: a driver-side batch validator / job-limit safeguard, a kernel with working SSE3 emulation, testing MPlayer with the *original* (pre-ycc) YCbCr table.
