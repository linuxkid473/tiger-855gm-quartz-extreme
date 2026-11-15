# GPU hang with hardware-GL VLC, the p54 wait-timeout bug, CPU downclock pin, Wi-Fi without ethernet (2026-11-15)

## 1. Hardware-GL VLC hangs the GPU (parked; the cause below is a hypothesis)
`vlc-hw` ([15](15-hardware-ycbcr-video.md)) froze the Dell three times in about an hour: twice on p56f (clicking the VLC controller while a video played), once on p54 (starting a video). QuickTime and normal (software-GL) VLC were not affected. p51 passed one test.

Live capture of the frozen state (ssh from another machine, Wi-Fi):
* **WindowServer ~86% CPU**, its main thread in `_CGXRunTimerPass -> CGXReloadWaitCursor -> reloadCursor -> MPSetCursor -> SysShowCursor/SysHideCursor -> MPPing -> IOPSBlitIdle -> _WaitComplete` (the spinning-beach-ball cursor is redrawn with a GPU idle wait for every frame, so the cursor still moves while the screen is dead). VLC's main thread is blocked in `_CGSSynchronizeWindowBackingStore` (waiting for WindowServer).
* **GPU stuck:** `lastReadStamp` frozen (33557) while `submitStamp` was 33648-33661 (91 queued jobs) and did not move.
* Registers (read-only kext, `tools/diag/gpuregs2.c`): ring `HEAD 0x2a8000ac` (offset 0xac) vs `TAIL 0xa0`, `ESR 0x11` (bit 0 = instruction error, new; bit 4 = page-table error, present since boot), `IPEHR 0x18800080` = the faulting instruction is the `MI_BATCH_BUFFER_START` of the job whose stamp would be `0x8316`. `IPEIR 0`. (`gpuregs.c` read IPEIR/IPEHR from the wrong offsets and printed zeros; correct ones are 0x2088 / 0x208c.)
* Ring dump (`tools/diag/gpudump.c`): every job is `MI_FLUSH, MI_BATCH_BUFFER_START <addr>, MI_FLUSH, MI_STORE_DWORD_INDEX 0x40 <stamp>, MI_REPORT_HEAD, MI_FLUSH`. The stuck batch at GTT 0x029c10e8 is state setup plus one `PRIM3D` quad (640x360, unnormalised texture coordinates) ending in `MI_BATCH_END`. The following job's batch at 0x029c11e8 starts with four dwords of garbage (`0ac12840 02600000 0 0`) before valid state.
* **A hung GPU cannot be revived.** `tools/diag/gpurevive2.c` (disable ring, unmask `EMR`, clear `EIR`/`PGTBL_ER`, `HEAD=TAIL`, re-enable): `EIR`/`ESR` stay `0x11`, `HEAD` stays 0, nothing executes. WindowServer leaves its wait loop (the spinning cursor then stalls only briefly) but the engine stays dead until a reboot. A ring-reset watchdog is therefore not an option on this chip.

**Hypothesis (found by reading my own p54 patch):** p54 replaced `IOSleep(1)` (10.4 ms per iteration on Tiger/x86) by `thread_block(0)` (microseconds) but scaled the loop limit only from 1000 to 2,000,000. The original wait gave the GPU about 10 s; p54 gives well under 2 s. When a heavy job (video frames) takes longer, `waitForTimeStamp` returns early and the driver can reuse command memory that the GPU has not consumed yet, which matches both the 91-deep queue and the garbage at the start of the next batch. **p54t** = the same patch with `LIMIT=200000000` (`python3 abi-port/mk_p54.py AppleIntel830.p53 AppleIntel830.p54t 200000000`, md5 `b249071491068ec5e3adcaf8025d7633`). It is installed as the default (`gfx-mode stable`), but hardware-GL VLC was *not* re-tested on it (decided to stay with QuickTime), so the hypothesis is unconfirmed.

Not suspects: the p56 cache-flush change (the hang also happened on p54 which keeps `wbinvd`), the CPU-pin kext (the first freeze happened before it was installed).

**Recommendation:** use `p54t` (`gfx-mode stable`). `p55`/`p56`/`p56f` were only validated with QuickTime/QE and are not recommended for anything using hardware GL.


## 2. CPU speed: periodic drops to 600 MHz (cpupin)
`hw.cpufrequency` always reports 1.59 GHz. A kext (`tools/cpupin`, plain C, `rdmsr`/`wrmsr`) reading `IA32_PERF_STATUS` every 100 ms found the CPU **dropping to the 600 MHz SpeedStep state about every 69 s with no thermal throttle** (`IA32_THERM_STATUS` bit 0 clear, ~58-60 C, critical 101 C). It writes the top state (`0x1028`, from the machine's ACPI `_PSS` table in `ioreg`) back into `IA32_PERF_CTL` within 100 ms, but never while the thermal throttle is active. Loaded at boot by a StartupItem with a crash guard. Cause of the periodic drop (platform plug-in / SMM / BIOS) not found. The "random slow motion for a few seconds" was this (and Dashboard widget start-up, which takes ~2 s of CPU the first time Dashboard is opened).

## 3. Wi-Fi without ethernet (iwi-connect v4)
The first WPA2 handshake after boot often failed ("timeout waiting for message 3"; 3 of the last 5 boots) and the kext retried only ~16 s later (about 50 s to get online), and the crash guard skipped a start after two unfinished starts. v4: `net.iwi.cmd=9` is re-issued immediately on `state=connect failed` (up to 12 quick retries), the guard is cleared as soon as the card answers (`up=1`), skipping needs three unfinished starts, and the wait loop polls every second. Tested with the cable unplugged over three reboots: all connected unattended (one first-attempt failure recovered in 1 s). ping answered 68-91 s after the reboot command.

## Tools added
`tools/diag/`: `gpuregs2.c` (ring/error registers), `gpudump.c` (dump GTT memory), `gpurevive2.c` (ring reset experiment), `slowmon.py` (monitor: loop speed, temperature, GPU stamps, auto-capture of WindowServer/Dock samples; run as root; disabled by default). `tools/cpupin/`. `gfx-mode stable|vsync -> p54t`, any staged variant by name.
