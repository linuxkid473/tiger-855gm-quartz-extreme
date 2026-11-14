# Hardware YCbCr video (VLC) on the 855GM

**Result:** VLC 0.9.10 plays 360p H.264 through the *hardware* renderer with correct colour, no dropped frames (0 "late picture" warnings in a 45 s run) and about **27% CPU / 65% idle**. With the software renderer the same player pegged the CPU at ~1 fps (85% of the video thread in the fragment-program emulator, see [14](14-cache-flush-video-vlc-wifi.md)).

## What was wrong
VLC 0.9.10's OpenGL output needs three extensions the hardware renderer did not advertise: `GL_EXT/ARB_texture_rectangle`, `GL_EXT_bgra`, `GL_APPLE_ycbcr_422`. Texture uploads of those formats actually succeed on the hardware renderer (no GL error), so it was only advertising plus one driver table bug.

### 1. Extension advertising (GLEngine 10.4.1)
`glGetString(GL_EXTENSIONS)` (`_glGetString_Exec`, vm 0x11000) first prints a fixed table of 25 strings, then loops over 73 capability bits supplied by the driver (word at `ctx->0x78c4 + 0x104`) and appends one string per set bit through a jump table (bit 23 = texture_rectangle, 30 = bgra, 44 = ycbcr_422, 4 = multitexture...). The hardware renderer returns none of the 73 bits (not even multitexture).
I did not find where the Intel driver would set them. Instead three rarely used entries of the fixed table (vm 0x12f340, slots 21-23: `NV_texgen_reflection`, `NV_light_max_exponent`, `IBM_rasterpos_clip`) are retargeted to the three strings. Pointers in that table are rebased by dyld, so only the stored vm address changes.

### 2. The format table (GLDriver, AppleIntel830GLDriver)
Texture state is packed in `gldRender` code around vm 0x15f35: `height-1 << 21 | width-1 << 10 | table[id*36 - 12]`. The table (vm 0x191e0, 9 dwords per record: GL format, GL type, GL internal format, bpp, id, flags, **map word**, two channel descriptors) holds the 855GM `TM0S1` low bits per format. Decoded against known formats (`0x40` I8, `0x48` L8, `0x88` ARGB1555, `0x90` ARGB4444, `0x98` AY88, `0xC0` ARGB8888 ...) the encoding is:

| bits | meaning |
|---|---|
| 8:6 | MAPSURF (1=8bit 2=16bit 3=32bit 5=**4:2:2**) |
| 5:3 | format within the class; for 4:2:2: 0=SWAPY 1=NORMAL 2=SWAPUV 3=SWAPUVY |
| 2 | **colour-space conversion** (YUV -> RGB in the sampler) |

Both YCbCr records already used MAPSURF_422 with colour conversion (`0x144` / `0x14c`) - the hardware converts YUV to RGB for free. They had the byte-order field the wrong way round: `YCBCR/8_8` (type 0x85ba, what VLC uploads) had NORMAL, `8_8_REV` had SWAPY.

### How it was found
With only the engine patch, VLC played with "radioactive" colour (green letter-box, magenta picture, vertical stripes: Y and CbCr columns interleaved the wrong way round). I built four driver variants that set both records to each of the four byte orders, played the clip with each and took a screenshot (`screencapture` over ssh works from the logged-in session):

| both records | picture |
|---|---|
| `0x144` SWAPY | **correct** |
| `0x14c` NORMAL | radioactive (the original behaviour for VLC) |
| `0x154` SWAPUV | radioactive |
| `0x15c` SWAPUVY | right shape, purple/blue tint (U/V swapped) |

Final: `8_8` -> `0x144`, `8_8_REV` -> `0x14c` (the other byte order, by symmetry; not exercised by VLC).

## Files
* `tools/ycc_hw.py` - applies both patches (`driver_in driver_out engine_in engine_out`); verified to reproduce the shipped binaries.
* `binaries/apple-derived/AppleIntel830GLDriver.home-variant.ycc` (md5 `9d2bca1c834ccad52a05e484e774ad86`) - HOME-variant driver ([08](08-video-playback.md)) with the table fixed. Install over `/System/Library/Extensions/AppleIntel830GLDriver.bundle/Contents/MacOS/AppleIntel830GLDriver` (keep a backup; WindowServer picks it up at the next boot, normal apps immediately).
* `binaries/apple-derived/GLEngine.1041.ycc` (md5 `91ff01fdd0f7500d810b300ab4e4b1a3`) - patched engine for the 10.4.1 `OpenGL.framework` copy at `/Library/OGL1041` (path-rewritten as described in [11](11-gl-frameworks-and-apps.md); the shipped file is that rewritten copy).
* `tools/system/vlc-hw` -> `/usr/local/bin/vlc-hw`: starts VLC on the hardware stack. It runs with an *empty* environment (`env -i`) because the HOME-variant driver rejects hardware for any process that has `HOME` set. Side effect: VLC cannot save preferences.

## Caveats
* Hardware GL for applications remains opt-in. Earlier, QuickTime video on hardware GL froze the machine; VLC survived 45+ s sessions here but a longer soak is still outstanding.
* Only the `8_8` (UYVY) path was tested visually.
* Core Image still cannot use the GPU (no fragment-program hardware).
