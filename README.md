# Quartz Extreme on Mac OS X Tiger 10.4.8 with an Intel 855GM (Dell Inspiron 510m)

Hardware-accelerated desktop compositing (**Quartz Extreme**) and accelerated 2D on a Dell Inspiron 510m (Pentium M, Intel Extreme Graphics 2 / **855GM**, PCI `8086:3582`) running a retail-ABI **Mac OS X 10.4.8** (Darwin 8.8.1) "Hackintosh". Apple never shipped an i830-family accelerator for the x86 retail 10.4.8 kernel; this project ports Apple's own developer-transition-kit (10.4.1 DTK) Gen2 accelerator + GL driver to 10.4.8, and then finds and fixes the hardware/driver problems that stopped 3D from working.

> **Status (2026-11-14):** System Profiler reports `Quartz Extreme: Supported`. The machine boots from a **cold power-on** straight into a QE desktop, survived a 9,000+ GPU-job window-wiggling soak and repeated reboots, and QuickTime video plays at ~30% CPU (640x360 H.264) with QE on, and (p53) window moves / Dashboard animation are tear-free. Core Image is *not* supported (the 855GM has no fragment-program hardware - not fixable). Ordinary applications use the software OpenGL renderer by default; hardware GL for apps is opt-in and partial ([docs/11](docs/11-gl-frameworks-and-apps.md)).

This is a research/port log as much as a tool: it documents every wrong turn, because most of the time went into finding that the final bug was not a GPU command problem at all: the 3D engine must be **primed** with one complete 3D state batch after power-up, and unused page-table entries must be valid.

## The short version of what was wrong (and the fix)

| # | Problem | Fix | Where |
|---|---|---|---|
| 1 | DTK 10.4.1 kext is built against a different C++ vtable layout than retail 10.4.8 (every kernel base class lost 16 reserved slots) | vtable rewrite + call-site slot translation | `abi-port/abiport.py`, patches p1-p3 |
| 2 | Accelerator waits on the wrong display pipe (LVDS is on pipe **B**; the code waits for pipe A scan-line) so the ring never advanced | always use pipe B (`83 e0 e2` -> `31 c0 90`) | patch p8 |
| 3 | 10.4.8 GLEngine needs gld* entry points the 10.4.1 GL driver lacks; 10.4.1 driver reads GLEngine internals at hard-coded offsets | pair the pristine DTK GL driver with the **10.4.1** OpenGL pieces, add 3 stub entry points, enable QE in CoreGraphics' `Configuration.plist` | `abi-port/glfix.py`, docs |
| 4 | 3D engine stalls when it touches an **invalid GTT page-table entry**; the kext never initialises unused entries | fill the whole GTT with the driver's scratch page at start-up | patch p46 (`mk_p46.py`) |
| 5 | `MI_FLUSH` after a batch needs `READ_FLUSH` (found by replaying Apple's batch on Linux) | `02000000` -> `02000001` | patch p49 |
| 6 | Chip registers differ from what Linux sets; `MI_ARB_STATE` is a *masked* register (write `0x00020002`, plain `2` is silently ignored) | write Linux's values at start-up | patches p47/p48 |
| 7 | **Root cause of the boot-time stall:** after power-up the 3D engine hangs on Apple's first compositor jobs until one complete, self-contained 3D state batch has run ("priming"); the primed state survives warm reboots, which is why every earlier fix only "worked" on warm boots | inject one IGT gen2 textured-copy batch into the ring at the first submission | **patch p51** (`mk_p51.py`) |
| 8 | ring/batch memory is written through cacheable CPU mappings (defensive: GPU could read stale RAM) | `wbinvd` before ring TAIL updates (p50), kept in the verified p51; the variant without it (p52) is optional | `mk_p50.py [nocache]` |
| 10 | Tearing in window drags / Dashboard (swap waits on a scan-line window) | wait for pipe-B vblank instead (`0x01800020` -> `0x01800080`) | **patch p53** (`mk_p53.py`), [docs/10](docs/10-vsync-p53.md) |
| 11 | GPU waits polled with `IOSleep(1)` = 10.4 ms per wait on Tiger/x86 (WindowServer ~45% blocked) | yield (`thread_block(0)`) instead of sleeping | **patch p54** (`mk_p54.py`), [docs/13](docs/13-gpu-wait-latency-p54.md) |
| 12 | whole-cache `wbinvd` on every GPU submission | flush just the 4 KB ring with `clflush` | **p56** (`mk_p56.py`), [docs/14](docs/14-cache-flush-video-vlc-wifi.md) |
| 13 | VLC video ~1 fps in the software renderer (hardware renderer lacks texture_rectangle / bgra / ycbcr_422; YCbCr byte order wrong in the driver's format table) | advertise the three extensions + swap the two YCbCr table records | **`tools/ycc_hw.py`**, `vlc-hw` launcher, [docs/15](docs/15-hardware-ycbcr-video.md) |
| 9 | QuickTime video uses 80-100% CPU under QE: QTKit takes the Core Image path, whose fragment programs run in the *software* GL emulator | force QTKit's QuickDraw ("GWorld") path: two 9-byte getter patches | `tools/qtkit_force_gworld.py`, [docs/08](docs/08-video-playback.md) |

Full story: [docs/02-investigation.md](docs/02-investigation.md). Exact byte patches: [PATCH_TABLES.md](PATCH_TABLES.md) and [docs/03-patch-reference.md](docs/03-patch-reference.md).

## What is (and is not) in this repository

**Included:** all source code, the patch generators (`mk_p4x.py`, `mk_p50.py`, `glfix*.py`, `gafix.py`), the ABI-translation tooling, kernel-extension sources and build scripts, diagnostic kexts (prebuilt, built from the sources here), Linux/GParted reference tooling, register dumps, test batches, and detailed documentation.

**Included binaries:** the patched `AppleIntel830` kext (p51 = cold-boot-verified, p53 = p51 + vsync, p54 = p53 + low-latency GPU waits, p56/p56f = cheap ring flush instead of wbinvd (snappiest; `f` = no vsync), plus the intermediate versions) in [`binaries/`](binaries/). These are Apple-derived (modified copies of the DTK 10.4.1 kext); see the notice in `binaries/README.md`.

**Not included:**
* The GL driver, GA plug-in, `OpenGL.framework` pieces, QTKit and kernels (Apple's). You need your own copies from a Tiger x86 DTK 10.4.1 image / your Tiger install ([docs/04-build-and-install.md](docs/04-build-and-install.md)). The patches are published as generator scripts and byte tables that you apply to *your* copy; every step lists the expected checksum so you can verify.
* Intel's `iwi-bss` firmware (Wi-Fi driver): generate `fw_bss.h` yourself with `tiger-kexts/mkfw.sh` from the firmware you obtain under Intel's licence.
* The kernel header tree (`xnu-792.13.8`): small stand-in headers are in `tiger-kexts/stub/` so the diagnostic kexts build without it.

## Repository map

| Path | Contents |
|---|---|
| [`docs/`](docs/) | Architecture, investigation timeline, patch reference, build/install guide, tools, recovery procedure, open issues, lessons |
| [`PATCH_TABLES.md`](PATCH_TABLES.md) | Generated (`tools/gen_patch_tables.py`) exact file-offset/old-bytes/new-bytes tables for the kext chain p1 ... p8, p45b, p48b, p50b, p51, p52 with checksums |
| [`binaries/`](binaries/) | Patched `AppleIntel830` kext variants (p51 verified from cold boot, p56/p56f snappiest), patched QTKit / GL driver / GA plug-in (`apple-derived/`) and the VLC plug-in patch, all with checksums |
| [`tools/`](tools/) | `qtkit_force_gworld.py` (video fix), `patchpaths.py` (GL framework-copy path rewrite), `hfsgrow.py`/`hfsverify.py` (in-place HFS+ grow), `gen_patch_tables.py`, GL extension-string experiments, `ext.rb` |
| [`abi-port/`](abi-port/) | `abiport.py` (vtable + call-site translation), `glfix.py`/`glfix2.py` (GL driver), `gafix.py` (GA plug-in), `mk_p44` ... `mk_p51.py` (kext patches), Mach-O/HFS+ readers, Ruby GL test kit |
| [`tiger-kexts/`](tiger-kexts/) | Kernel-extension sources: `gttwatch`, `gttfill`, `gpuregs`, `gpudump`, `gpupoke`, `mmiodiff`, `gpustress` (test harness that submits batches through Apple's own ring path), and the Intel 2200BG Wi-Fi driver; build scripts; `prebuilt/*.tar` |
| [`bootdump/`](bootdump/) | Linux (GParted Live + i915) reference tooling: GEM batch emitters (Mesa-i830 style and an IGT gen2 port), a ctypes ioctl runner, MMIO register dumps from Linux and from a cold BIOS boot, a 16-bit real-mode register dumper |
| [`tiger-ethernet-510m/`](tiger-ethernet-510m/) | Notes + scripts for the PRO/100 VE Ethernet kext fix (one-instruction patch) |

## Quick start (high level)

1. Obtain the inputs (your own legal copies) - see the build guide.
2. `python3 abi-port/abiport.py vtables` / `calls` to translate the DTK kext, apply the p2-p8 byte edits (JSON/tables), then `mk_p45.py` -> `mk_p48.py` -> `mk_p50.py` -> `mk_p51.py` (= p51, the verified kext; p52 = same without the per-submit `wbinvd`, optional). Verify the md5 of each stage against [PATCH_TABLES.md](PATCH_TABLES.md) (only valid if your input was byte-identical to mine).
3. Install the kext, the pristine GL driver, the 10.4.1 OpenGL pieces and the QE `Configuration.plist`, rebuild the kext cache, reboot.
4. For video: `python3 tools/qtkit_force_gworld.py QTKit QTKit.new` and install it ([docs/08](docs/08-video-playback.md)).

**Warning:** a wrong kernel-extension patch can freeze the machine at boot. Read [docs/06-recovery.md](docs/06-recovery.md) *before* installing anything: it contains the recovery procedure (boot Linux from a USB stick, patch the file bytes in place on the journaled HFS+ volume, neutralise the kext cache).

## Requirements / assumptions

* Dell Inspiron 510m or another **855GM (`8086:3582`)** laptop; Pentium M; 1 GB+ RAM; LVDS panel on pipe B.
* Mac OS X 10.4.8 i386 (Darwin 8.8.1 retail ABI). Kernel was a community ("semthex"/JaS) 10.4.8 build.
* A second machine with SSH access to the Dell makes development *much* easier: the Dell stays reachable over Ethernet even while its GPU is wedged.

## License and notices

Original code and documentation in this repository are released under the MIT License (see `LICENSE`). Apple, Mac OS X, Quartz Extreme, QuickTime and Tiger are trademarks of Apple Inc.; Intel and 855GM are trademarks of Intel Corporation. This project is not affiliated with or endorsed by either company. The source and docs contain no Apple or Intel proprietary code; `binaries/` holds Apple-derived kext binaries (see its README). The Linux/Mesa/IGT register and command-layout facts used here were read from the public GPL/MIT sources; no GPL code is included in the kext sources.

## Credits

Research, reverse engineering and writing: the repository author, working with an AI coding assistant (Claude). Reference behaviour from the Linux `i915` driver, Mesa's i830 driver and `intel-gpu-tools`.
