# Investigation timeline

How Quartz Extreme (QE) went from "black screen" to "boots straight into a QE desktop", including every wrong turn. Patch names (`p8`, `p46`, `p51`, ...) refer to [PATCH_TABLES.md](../PATCH_TABLES.md) and [03-patch-reference.md](03-patch-reference.md).

## Stage 0 - Can Apple's own driver be used at all?

Apple never shipped an i830-family accelerator for x86 *retail* 10.4.8. The only copy is in the developer-transition-kit (DTK) 10.4.1 image: `AppleIntel830.kext`, `AppleIntel830GA.plugin`, `AppleIntel830GLDriver.bundle`.

* **The kext does not load on 10.4.8 as is.** Between 10.4.1 and 10.4.8 xnu removed 16 reserved ("padding") virtual slots from every IOKit/libkern base class, so every virtual call in the DTK kext lands in the wrong slot. `abi-port/abiport.py` rewrites the kext's vtables and translates every call site (`vtables` / `calls` sub-commands). Result: the kext loads, starts and initialises the 855GM (p1-p3).
* `IOIntel830Accelerator::start` only looked for framebuffers on IOKit <= 6, so two conditional jumps are NOPed (p5).

## Stage 1 - Black screen: the ring never moves (p8)

With the kext loaded WindowServer logged `Accel caps: 00000001` and the screen went black. The accelerator statistics showed `submitStamp=2, lastReadStamp=0`: the GPU never consumed the ring.

Cause: the Inspiron's LVDS panel is on display **pipe B** (LVDS register 0x61180 bit 30 set, plane A disabled) but the driver emits `MI_WAIT_FOR_EVENT` for the pipe-A scan line (`0x01800002`) for framebuffer 0, so the ring waits for an event that never comes. Patch `83 e0 e2` -> `31 c0 90` makes it always pipe B (`0x01800020`). With p8 `lastReadStamp` tracks `submitStamp`; **2D acceleration works** (this is the stable fallback, still the base of every later kext).

## Stage 2 - Why QE stayed "Not Supported" (GL side)

* 10.4.8's `GLEngine` needs 61 `gld*` exports; the 10.4.1 GL driver lacks three framebuffer entry points. They are added as `xor eax,eax; ret` stubs (`glfix.py`), and QE is enabled in CoreGraphics' `Configuration.plist`.
* The 10.4.1 GL driver reads GLEngine's internal state at hard-coded offsets, so with the 10.4.8 GLEngine it emits garbage commands (`Instruction error`). The cure is pairing the **pristine** DTK GL driver with the **10.4.1** OpenGL pieces (GLEngine, GLRendererFloat, GLRaster*, GLDriver, libGLImage, libGLProgrammability). After that all texture uploads work and the QE path runs.

## Stage 3 - The 3D engine stalls (the long hunt)

QE ran, then the 3D engine froze after a random number of jobs (2, 50, 378, 3143 ...). Signature: ring `HEAD` stuck on the post-batch `MI_FLUSH`, `INSTDONE = 0x037b10c1`, `lastReadStamp` stops while `submitStamp` runs on. Hypotheses tested and **disproved**:

| Hypothesis | Experiment | Result |
|---|---|---|
| texture/GTT mapping, fences, tiling | p16-p23 probes | not the cause |
| vertex/state/depth formats | p24-p33 batch rewrites | no change; batch rewrites hard-froze the machine |
| watermarks / chip registers (FW_BLC, MI_ARB_STATE) | p34-p36, p47/p48 | no change |
| missing in-batch flush (Linux's cure) | p43 | no change |
| race on batch memory | p44 (inline copy into the ring) | no change |
| serialising every batch | p40/p41 | froze the machine |

Two reference points made the search tractable:

1. **Linux i915 (GParted Live USB) proves the 855GM does textured 3D**; `bootdump/` has the Mesa-i830-style and IGT gen2 batch emitters, register dumps (cold BIOS vs. Linux) and a ctypes runner.
2. A **test harness kext** (`gpustress`) submits batches *through Apple's own `Intel830::submit_buffer`*. 4000 runs each of a no-op, a blit, the Linux-proven textured copy and Apple's *captured stalled batch* ran without a stall - so the chip, the ring and the batches were fine, and the difference had to be in system state.

## Stage 4 - Invalid GTT entries (p46)

Tiger leaves unused page-table entries invalid (`PGTBL_ER 0x49` sticky); the fenced X-tiled region has every other page unmapped. A batch/prefetch touching an invalid entry hangs the engine. p46 fills the *entire* GTT with the driver's scratch page at start-up (as Linux does). Stalls moved from job 2 to job ~50 and later, and QE became usable - but not from a cold boot.

## Stage 5 - Everything so far was a warm reboot

A hard power cycle brought the stall back at the very first jobs. All earlier "successes" were warm reboots: page table and chip registers survive them. p46's fill and the register experiments had never been validated from a cold state.

* **p50** (`wbinvd` before every ring-tail write) was developed as "the" fix after finding that the ported kext writes ring/batch memory through cacheable mappings. It reduced but did not remove the cold-boot stall, and it is only defensive.

## Stage 6 - The real root cause: priming (p51)

Discriminating experiment: after a power-up the 3D engine hangs on Apple's very first compositor jobs **until one complete, self-contained 3D state batch has run**. Running the Linux-proven IGT textured copy once (via the harness) "primes" it, and the primed state then survives warm reboots (even across QE-off boots). Sequence: wedged QE boot -> restore stock config + reboot -> run the harness (4000 IGT copies, correct pixels) -> QE config + warm reboot = QE desktop works.

p51 therefore **injects one IGT gen2 textured-copy batch into the ring at the first submission** after the kext starts (flag byte in a code cave; the batch is built on the stack and copied into the ring before the stock `BB_START` emission). Verified: a real power-on boots straight into a stable QE desktop.

## Stage 7 - Video playback under QE

See [08-video-playback.md](08-video-playback.md): QuickTime was white (hardware renderer missing extensions), then crawling (software GL emulating fragment programs), finally fixed by forcing QTKit's QuickDraw path.

## What was not the cause

CPU idle states, thermal state, maxmem, GPU "wedge persistence" (the GPU cannot be reset in software), `READ_FLUSH` alone, and the Apple GL driver/GA patches.
