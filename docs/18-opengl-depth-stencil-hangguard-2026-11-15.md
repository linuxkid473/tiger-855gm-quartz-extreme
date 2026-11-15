# OpenGL on the 855GM: iTunes visualizer, depth buffer, Chess, and the GPU-hang guard (2026-11-15)

Unattended session, goal: "make OpenGL fully functional". What was found, what was fixed, what is still open.

## 1. Why the iTunes visualizer was "ultra-glitchy"
Normal apps get the **software** renderer (HOME-variant driver, docs/11). iTunes' visualizer uses fragment-style per-pixel work that
the software renderer runs through its CPU **fragment-program emulator** (`sample iTunes`: `glEnd -> gldTessellate... -> gldFPEmulateScanline ->
PPEmulatorRun`). On a 1.6 GHz Pentium M that is ~1 frame/s and the visualizer only shows the idle Apple logo.
On the **hardware** stack (`/Library/OGL1041`, no HOME) the same visualizer renders correctly at ~50 % CPU, windowed (fullscreen could not
be verified remotely: `screencapture` shows black for a CGL fullscreen context; the machine stayed healthy).
**Fix:** `iTunes.app/Contents/MacOS/iTunes` is now a wrapper (real binary `iTunes.bin`) that runs it on the hardware stack
(`tools/system/app-wrapper-hw`).  `tools/system/glhw <cmd>` does the same for any program.

## 2. The hardware depth/stencil buffer (all fixed)
Test kit: `tools/diag/gltest/` (Ruby DL, no compiler needed; AGL window + `glReadPixels`).
* `glClear(GL_DEPTH_BUFFER_BIT)` **works** on the stock driver *if the depth test is enabled when the clear is issued* (the engine skips the depth
  clear while the depth test is off, so a one-shot "clear, then enable depth test" leaves the buffer at 0; real apps clear every frame).
  Several driver patches for the clear (RECTLIST -> TRIFAN, depth-test/ALWAYS/stencil-write state, DEFAULT_Z scale) turned out unnecessary and are not shipped.
* **Readback was wrong because depth and stencil share ONE 32-bit word** (depth bits 23:0, stencil bits 31:24; ring/surface dumps show `00ffffff` after a depth clear).
  The driver's readback stage described that buffer to the user-space converter as a plain `GL_UNSIGNED_INT` depth plane (stencil: a separate byte plane that does not exist), so
  depth reads were scaled 1/256 and polluted by stencil (stencil 5 -> depth 5.5) and stencil reads returned 0.  libGLImage's `glgProcessDepth/Stencil` already support a
  packed source (format 0xf, type 0x15 = depth low 24 bits / stencil top byte); `tools/gldriver_depthstencil.py` makes the driver's readback stage (vm 0x10dfc: depth block
  at 0x1123d/0x11253, stencil block redirected to a cave) use it.  The kernel's extra copy modes (selector 7, modes 0xa-0xe) are not implemented, so raw mode 4 + user-space unpacking is used.
  Result: depth reads exactly 0.2500/0.5000/1.0000, stencil reads exactly 5/0 (`stencil-depth-packing.rb`, `depth-clear-multiframe.rb`).
* The patched driver is a **private copy** (`/Library/OGL1041/Ext/AppleIntel830GLDriver.bundle`): the opt-in GLEngine's driver search path is patched from `/System/Library/Extensions/` to
  `/Library/OGL1041/Ext/`, so only processes on the hardware stack (glhw, wrapped iTunes/Chess) load it; WindowServer and normal apps keep the stock driver.

## 3. Chess
**Chess now works on the hardware renderer**: the piece highlights, the move registers, the engine replies, the board redraws.  `Chess.app` is wrapped (real binary `Chess.bin`, wrapper = hardware stack).
It must be started through LaunchServices (Dock/Finder/`open -a Chess`); running `Chess.bin` directly from a shell leaves it unable to become the active app.
(The default mixed stack ghosts/blanks GL drawing; `glsw` = consistent 10.4.8 software stack draws correctly but its window did not visibly repaint after moves in the remote test.)

## 4. Automatic reboot on a GPU hang (tools/hangguard)
`hangguard.kext` samples the 855GM ring registers every second; if HEAD != TAIL, neither HEAD nor ACTHD moves for 20 s **and the GPU has latched an error (EIR/ESR non-zero, the signature of the observed hangs)** (after a 150 s grace
period, and not on the boot after a watchdog reset) it resets the machine through port 0xCF9.  The error condition keeps an idle-but-queued ring (display asleep) from rebooting the machine.  Installed at boot by a StartupItem with the same crash guard as
`cpupin`.  **Limits:** (a) the ICH4 TCO hardware watchdog was fully tested (PMBASE 0x800, NO_REBOOT cleared, TCO SMI and interrupt disabled; the timer counts down
and latches TIMEOUT) but its second timeout never resets this Dell, so a total kernel deadlock cannot be caught; (b) after the reset the machine stops at the login window
unless **Automatic login** is enabled (System Preferences > Accounts > Login Options) - it was NOT enabled when this was written.

## 5. Other findings
* `ringdump.c` (read-only kext) dumps the newest ring batches / raw surface memory over the aperture: state words `0x63..` (ENABLES), `0x64` (ENABLES_2),
  `0x62` (MODES_3 = depth func), `0x7d8e0001` (BUF_INFO: colour `03802000 <addr>`, depth `07802000 <addr>`, pitch 8192, tiled), `0x7d98/0x7d99` (DEFAULT_Z/DEFAULT_DIFFUSE).
* GL selection-mode picking (`glRenderMode(GL_SELECT)`) works identically on hardware and software (`selection.rb`).
* Driver code caves: zero padding in `__cstring` (vm 0x16864.., 0x181e8, 0x18246) is executable and was used for the experiments.

## 6. Hardware GL as the default for all apps: `glmode on`
* The private driver (`AppleIntel830GLDriver.depthstencil`) no longer has the HOME-variant check: any process on the `/Library/OGL1041` stack gets the hardware renderer
  (`GL_REJECT_HW=1` forces the software one for a single process).
* `tools/system/glmode on <user>` writes `~/.MacOSX/environment.plist` (`DYLD_FRAMEWORK_PATH`/`DYLD_LIBRARY_PATH` -> `/Library/OGL1041`), so every app of that login session gets hardware GL.
  It takes effect at the **next login**; `glmode off <user>` removes it.  WindowServer and the /System driver are untouched, so a bad session cannot stop the next boot.
* Checked with the redirect + HOME set: the AGL tests (hardware renderer, exact depth/stencil), Flurry fullscreen, iTunes, Chess, QuickTime Player launches.
* **Opt-out for the risky video apps:** VLC, QuickTime Player and DVD Player are wrapped with `tools/system/app-wrapper-stock` (real binary renamed `<App>.bin`; the wrapper unsets
  the DYLD variables, i.e. the machine's old default GL setup), so `glmode on` does not touch them.  Verified: with the redirect set in the caller's environment the wrapped
  VLC and QuickTime processes have no `DYLD_FRAMEWORK_PATH`.  Do NOT use `app-wrapper-sw` (`/Library/OGL1048`) for QuickTime: it fails with "invalid pixel format" and crashed.
* Other caveats: any other app that is not safe on hardware GL needs the same wrapper; PPC (Rosetta) GL apps cannot use the redirected frameworks (docs/11).

## 7. VLC HW as a proper app
The first `vlc-hw` exec'd `VLC.bin` straight from a shell script, so VLC was not registered with LaunchServices (no Dock icon / menu bar / Cmd-Q).  Now VLC always starts through
LaunchServices: `VLC.app/Contents/MacOS/VLC` is `tools/system/app-wrapper-vlc` (real binary `VLC.bin`); it uses the stock GL setup by default, and uses the hardware stack for one launch when
`vlc-hw` (the VLC HW icon) has just touched `/tmp/.vlc-hw-request` and called `open -a VLC`.  Do not pass `--no-plugins-cache` together with LaunchServices' `-psn_` argument: VLC dies during startup.
Verified with the hardware stack in the process environment: menu bar/Dock icon correct, `tell application "VLC" to quit` ends it, video plays with correct colours.
