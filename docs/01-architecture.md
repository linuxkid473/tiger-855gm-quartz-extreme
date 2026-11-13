# 1. Architecture: what has to work for Quartz Extreme on Tiger

## The stack

```
WindowServer (CoreGraphics)          composites every window as a GL texture when "Quartz Extreme: Supported"
   |  CGL / GLEngine (OpenGL.framework/Resources/GLEngine.bundle)
   |  gld* entry points (≈61 of them)            <- the vendor "GL driver plug-in"
AppleIntel830GLDriver.bundle  (user space)       emits Gen2 3D command buffers (descriptor chain in shared memory)
   |  IOConnect user-client calls (Mach IPC)
AppleIntel830.kext  (kernel)                     Intel830GLContext / Intel8302DContext / Intel830Surface user clients,
   |                                             ring buffer, GTT/aperture management, stamps (job fences)
855GM hardware: ring buffer -> 3D engine, GTT, display B (LVDS)
AppleIntel830GA.plugin (user space, CFPlugIn)    2D "GA" accelerator interface used by WindowServer (blits, surfaces, capabilities)
```

Apple built this stack for i830/845/855/865 in the **10.4.1 developer-transition-kit** era (x86 DTK) and dropped it from retail 10.4.x x86 (GMA950 replaced it). The retail 10.4.8 kernel and OpenGL.framework no longer match the DTK pieces, hence the ABI work.

## Pieces used by the final working setup

| Piece | Source | State |
|---|---|---|
| `AppleIntel830.kext` | DTK 10.4.1 | ABI-translated + patches p1..p50 (this repo) |
| `AppleIntel830GLDriver.bundle` | DTK 10.4.1 | **pristine** (the earlier `glfix2` capability cave is obsolete) |
| `AppleIntel830GA.plugin` | DTK 10.4.1 | pristine (see [open issues](07-open-issues.md) for the capability patch experiments) |
| OpenGL pieces: `GLEngine`, `GLRendererFloat`, `GLRasterARGB8888D16/D32`, `GLRasterRGB555A8D16/D32`, `GLDriver`, `libGLImage`, `libGLProgrammability` | **10.4.1** image | replace the 10.4.8 files (backup first) |
| `CoreGraphics.framework/Resources/Configuration.plist` | edited | `GLCompositorRequiredClasses` emptied, `EnableErrorLogging` true |

Why the 10.4.1 OpenGL pieces: the 10.4.1 GL driver reads GLEngine's internal GL state at **hard-coded offsets** (e.g. `state+0x2d24`). Against the 10.4.8 GLEngine it emitted garbage commands (`fffc3300` after `ENABLES_1`, "Instruction error"). Paired with its own era's GLEngine it emits sane batches. (We first tried to bridge this with a capability-record cave, `glfix2.py`; the matching-era pairing made it unnecessary.)

## The Gen2 command path (what the GL driver sends)

* One shared command buffer per context: a **descriptor chain** (`header = type<<24 | length`). Types 1/`c` = textures, `8/9/a/b` = state buffers / stamp, `0x10/0x20` nibbles.
* The kernel's `Intel830GLContext::process_command_buffer` (vm `0x14248`) patches descriptors **in place** (type `a` -> `_3DSTATE_BUF_INFO` payload, type `b` -> `MI_FLUSH + MI_STORE_DWORD` stamp, textures -> `LOAD_STATE_IMMEDIATE_2` words) and `Intel830::submit_buffer` (vm `0x12148`) queues the batch.
* Ring emission per job (10 dwords): `MI_FLUSH|READ_FLUSH`, `MI_BATCH_BUFFER_START <gtt>`, pad, `MI_FLUSH`, `MI_STORE_DWORD_INDEX 0x40 = stamp`, `MI_REPORT_HEAD`, `MI_FLUSH|READ_FLUSH`. The stamp is the job fence; `lastReadStamp` vs `submitStamp` in the accelerator's `PerformanceStatistics` is the quickest health check (`ioreg -l | grep Stamp`; healthy = they differ by 2-14).
* Batch addresses live in a fixed GTT arena (context template at `0x0202c020`, setup `0x0202d020`, draws `0x0202d130`..., textures `0x0203d000`+), the colour buffer is the tiled `0x04000000` surface (1024x768, pitch 8192, fence 7), depth `0` (display scan-out area!) with depth write disabled.

## Hardware facts established (855GM / Gen2)

* BAR0 = 128 MB aperture, BAR1 = MMIO. GTT entries are written through **MMIO+0x10000 (write-only!)**; the table itself is in stolen memory and is not CPU-readable. Unmapped aperture pages read back `0xf000eec2` - that is how mapping state can be probed.
* `PGTBL_CTL (0x2020) = 0x4ffe0001` (128 KB table = 32768 entries), `PGTBL_ER (0x2024) = 0x49` is **sticky** from boot (display B prefetch of an invalid entry) and harmless.
* Fence 7 (`0x201c`) = `0x04000761`: X-tiled, 64 MB..128 MB, pitch 8192; Gen2 tiles are 128 B x 16 rows (2 tiles/page).
* `MI_ARB_STATE (0x20e4)` is a **masked** register (bits 31:16 are write-enables): write `0x00020002`.
* The GPU **cannot be reset** in software (DEBUG_RESET, PCI D3hot, GDRST all fail): a hung 3D engine needs a reboot, and a cold boot is the only guaranteed clean state.
* `MI_FLUSH` bit0 = READ_FLUSH (invalidate map/texture caches).
