# Tear-free swaps (p53)

**Symptom (p51):** windows tear while being dragged and the Dashboard show/hide animation tears.

**Cause:** `Intel830Surface::build_swap` builds the swap command buffer: `MI_FLUSH`, `MI_LOAD_SCAN_LINES_INCL` (window = the swapped rectangle) and an `MI_WAIT_FOR_EVENT` for the *scan-line window* of one display pipe, then the textured copy to the front buffer. The wait event is `0x01800000 | pipe_bits`. Patch p8 (see [03](03-patch-reference.md)) forced the pipe selection to **pipe B** (`xor eax,eax`), giving `0x01800020` (pipe B scan-line window). That keeps the ring alive on the 510m but does not line the copy up with the panel refresh.

**Fix (p53):** change that one immediate to `0x01800080` = `MI_WAIT_FOR_EVENT | PIPEB_VBLANK` (bit 7). The copy now starts at the start of vertical blank and runs ahead of the beam (a full-screen copy is faster than the 16 ms scan-out), so there is no visible tear.

```
vm 0x18a33 (file offset 0x18d0f):  05 20 00 80 01   ->   05 80 00 80 01      add eax, 0x01800020 -> 0x01800080
```

`abi-port/mk_p53.py` applies it to a p51 executable (`md5 05526065...` -> `9000d051b0f65c1db3e20b447acbf411`). Binary: `binaries/variants/AppleIntel830.p53`.

**Result:** user-confirmed tear-free window dragging and Dashboard animation on the 510m. Costs: every swap now waits up to one refresh (max 60 swaps/s), so extremely swap-heavy apps are capped at the panel rate.

**Caveats / honesty:** installed and run for one session only. During that session the machine went off the network and was found rebooted; the cause is unknown (no log survives a hard lock-up; the same session was also running experiments). If you see lock-ups, go back to p51 (`variants`/main kext) first - that is the verified-from-cold-boot kext.
