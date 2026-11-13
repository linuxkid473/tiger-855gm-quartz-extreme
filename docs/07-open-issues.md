# Open issues and limits

| Item | Status |
|---|---|
| **Core Image** | Not supported. The 855GM has no fragment-program hardware; the software float renderer emulates fragment programs on the CPU (slow). Not fixable. |
| **Hardware OpenGL for applications** | Ordinary apps are deliberately kept on the software renderer (`GL_REJECT_HW` -> `HOME` trick) because the hardware renderer lacks `GL_APPLE_ycbcr_422`, `texture_rectangle` and `bgra` (25 vs 45 extensions) and offscreen rendering. Only WindowServer's compositor uses the 3D engine. |
| **Video** | Fixed with the QTKit GWorld patch ([08](08-video-playback.md)): ~30% CPU for 640x360 H.264 on a Pentium M. Larger/more demanding videos were not measured. VLC was not measured. QTKit apps lose Core Image video effects. |
| **Cold boot of p52** | p51 was verified from a real power-on; p52 (p51 without the per-submit `wbinvd`) booted and ran from a warm reboot only. If a cold boot ever stalls, go back to p51 (the cache flush is defensive). |
| **Why priming is needed** | The IGT textured-copy batch cures the post-power-up hang, but the *mechanism* (which 3D state Apple's first jobs miss) is not understood. Plausible: some state the Linux/IGT batch sets explicitly is assumed by Apple's first compositor batches to have been set by an earlier full-state batch. |
| **Cache coherency** | The ported kext writes ring/batch memory through cacheable mappings; `wbinvd` per submit is a blunt instrument and not needed once the engine is primed. A proper fix would restore the cache attribute (write-combining/uncached) that the 10.4.1 kext set up. |
| **Dashboard widgets vanish** | Restarting WindowServer (e.g. while testing QE) crashes `DashboardClient`; widgets are lost when it restarts. A reboot does not do this. |
| **Wi-Fi start after interrupted boot** | The `iwi-connect boot` crash guard (`/var/db/.iwi.starting`) treats a start that was interrupted by a reboot as a crash and skips the next start. Workaround: `sudo iwi-connect boot` manually, or have the startup item's `StopService` remove the guard. |
| **Dell-specific** | Everything is verified on one machine (Inspiron 510m, 855GM `8086:3582`, LVDS on pipe B). Other 855GM laptops with a different pipe/panel arrangement may need a different p8 patch. |
