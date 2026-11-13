# Lessons

1. **Validate from a cold state.** Page tables and chip registers survive warm reboots (and Linux sessions leave them valid). Months of "fixes" were validated on a primed chip. When a fix "works", power-cycle before believing it.
2. **A stress harness that always flushes caches hides cache bugs.** The harness never stalled because it ran `wbinvd`; the real driver stalled. Make the test path as dumb and as faithful as the production path.
3. **Replay the failing input on a known-good stack.** Running Apple's captured stalled batch under Linux/i915 (and through Apple's own submit path) proved the batch and ring were fine and moved the search to system state.
4. **Distinguish "required" from "defensive" changes.** `wbinvd` per submit looked like the root cause and made things *better*, but the real cause was priming; the flush later turned out to be unnecessary.
5. **Profile before theorising.** `sample` showed in seconds that the "slow video" was QuickTime running Core Image in the CPU emulator, not the kernel or WindowServer; hiding extension strings (my first guess) never could have worked.
6. **Read the binary for the decision, not just the symptom.** QTKit's imaging-mode getters were two 9-byte functions; patching them was cheaper than any amount of GL emulation tuning.
7. **Zero runs are not always free space.** In Mach-O `__cstring` they are string slots; in the DTK kext the dead `dma_test` body is the safe cave. Check relocations before placing code.
8. **Plan recovery first.** Same-size in-place patching from a Linux stick, a neutralised `Extensions.mkext`, a staged `kextload` loop and always-kept fallbacks turned several hard freezes into cheap iterations.
