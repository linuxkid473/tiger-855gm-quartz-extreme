# Hardware OpenGL for applications: what works and how it is arranged

## The version-mismatch finding
The QE setup runs Apple's **10.4.1** DTK GL driver. That driver needs the **10.4.1** `GLEngine` and OpenGL pieces. Running 10.4.1 pieces under 10.4.8 `libGL`/applications caused ghosting / white labels in *every* GL application (hardware or software renderer). Two self-consistent stacks render perfectly:

* a **complete 10.4.1 `OpenGL.framework` copy** (with the DTK hardware driver) -> correct hardware rendering (Chess, Flurry, Safari compat checked);
* a **stock 10.4.8 copy** -> correct software rendering.

## Per-process redirection
* `DYLD_FRAMEWORK_PATH=/Library/OGL1041` redirects framework loads only. Dependent *dylibs* (`libGL.dylib` pulled in by AGL/SDL) additionally need `DYLD_LIBRARY_PATH=/Library/OGL1041/OpenGL.framework/Versions/A/Libraries`.
* The framework copies contain hard-coded `/System/Library/Frameworks/OpenGL.framework` strings; `tools/patchpaths.py` rewrites them (same length constraints) to `/Library/OGLxxxx/OpenGL.framework` so a redirected GLEngine does not pull the system copy back in.
* User-wide default: `~/.MacOSX/environment.plist`. **Do not make hardware GL the default** (below).
* Per-process software: `GL_REJECT_HW=1` (the driver's own switch; the earlier HOME-rename hack of the driver is the system-wide equivalent).

## Recommended configuration: WindowServer on hardware, apps on software, hardware opt-in
Making hardware GL the default for all apps froze the machine when QuickTime Player started video. The safe default is: the **HOME-variant** GL driver (apps get the software renderer, WindowServer/QE keeps the hardware) and **no** `environment.plist`. Games that want hardware GL are launched with the `DYLD_*` variables above. Video then plays as in [08](08-video-playback.md).

## Application results (Dell 510m, 855GM)
| App | Result |
|---|---|
| Chess, Flurry, Safari | correct on hardware with the 10.4.1 framework copy |
| Extreme Tux Racer | needs `glLockArraysEXT` stubbed (two call sites, 5-byte NOPs / early return); runs ~14 FPS but terrain/Tux are not drawn on the hardware renderer (skybox + HUD only) - unresolved, suspected unimplemented draw path in the hardware renderer |
| Halo (Mac, 2.0.4) | hard-freezes the machine on hardware GL (rendering needs an accelerated renderer, so the software renderer is refused) - unresolved, avoid |
| Quake 3 (PPC build) | crashes under Rosetta: the Rosetta shim hard-codes the `/System` GLEngine path and the 10.4.1 libGLImage needs `_sqrt`; PPC apps cannot use the redirected pieces |
| Dock magnification | occasionally stops animating until `killall Dock` (clicks still work). Samples show Dock idle and WindowServer busy in software window-transform downsampling (`CGSDownsampleARGB`) and waiting on blit-idle; see `docs/samples/dock-magnify-stall-sample.txt`. Not understood. |

The hardware renderer lacks `GL_APPLE_ycbcr_422`, `texture_rectangle` and `bgra` (25 vs 45 extensions); 3D games that need more than basic fixed-function rendering are the main remaining gap.
