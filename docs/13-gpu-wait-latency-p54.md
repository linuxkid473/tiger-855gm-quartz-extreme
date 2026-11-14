# GPU wait latency (p54)

**Finding.** Profiling WindowServer (`sample`) while windows were being moved showed ~45% of its samples blocked in the accelerator, in two places: `CGXGLWaitTexture -> glFinishObjectAPPLE -> gldFinishObject` (waiting for a texture upload to retire) and `CGXUnshieldCursor -> IOPSBlitIdle -> _WaitComplete` (the software cursor waits for the blitter to go idle before it is redrawn). Both end in `Intel830::waitForTimeStamp`, which polled the GPU's stamp with `IOSleep(1)` per iteration (up to 1000 iterations).

On Tiger/x86 the timer tick is 100 Hz: measured on the 510m, `time.sleep(0.001)` takes **10.4 ms**. So every CPU<->GPU wait whose stamp had not already retired cost at least one full tick, several times per frame.

**Fix (p54, on top of p53).** In `waitForTimeStamp` replace the `IOSleep(1)` call by `thread_block(0)` (a yield; `_thread_block` is already imported by the kext, so only the relocation's symbol index changes) and scale the two loop limits from 1000 to 2,000,000 so the time-out stays in the seconds range.

| edit | before | after |
|---|---|---|
| vm 0x11f4d | `c7 04 24 01 00 00 00` | `... 00 00 00 00` (argument 0) |
| reloc @0x11f55 | `_IOSleep` | `_thread_block` |
| vm 0x11f72 | `81 f9 e7 03 00 00` | `81 f9 7e 84 1e 00` |
| vm 0x11fb8 | `81 fe e9 03 00 00` | `81 fe 80 84 1e 00` |

`abi-port/mk_p54.py` applies it to a p53 executable (`md5 9000d051...` -> `988258e0c102c31e1b0514d0c3109d0f`).

**Result.** Subjectively smoother (user: "it's better now"): window drags and Dashboard. A scripted Finder window-move benchmark (120 `set bounds` calls) barely changed (1.44 s -> 1.35 s) because it is limited by AppleScript/Finder, not by the compositor, so it is not a good frame-time measure.

**Not changed / ideas.** `sleepForTimeStamp` and the ring-space wait in `submit_buffer` still use `IOSleep(1)` (rarely hit). Remaining costs seen in the samples: the software cursor shield/unshield, software window-transform downsampling (`CGSDownsampleARGB`, Dock magnification), and the per-submit `wbinvd` (p50/p51; p52 removes it but is unverified from cold).
