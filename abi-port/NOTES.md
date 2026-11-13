# Porting Apple's Gen2 (AppleIntel830) accelerator from the 10.4.1 DTK build to 10.4.8

Goal: make the DTK-era `AppleIntel830.kext` (matches 855GM 0x3582) load on a retail-ABI 10.4.8 kernel.
Binaries are NOT stored here (Apple's, from the user's own images). Scratch copies lived in the session scratchpad.

## Findings
* `kextload -t -v 6` fails in "patching C++ code": `14Intel830Memory is not compatible with its superclass, 8OSObject superclass changed?`
  (kld vtable check). Cause: C++ vtable layout of the kernel base classes differs between DTK 10.4.1 and retail 10.4.8.
* Old (10.4.1) OSObject vtable: 1 dtor slot (D0 only), 32 reserved slots. New (10.4.8): D1+D0, 16 reserved slots.
  Per-level shift measured from the kext's own relocations: OSObject-level slots +1, IORegistryEntry-level -15,
  IOService/IOUserClient-level -31 (every level lost 16 reserved slots).
* Layouts rebuilt from: 10.4.1 `mach.sym` + kernel image (170 vtables) and the JaS/10.4.8 kernel (91 vtables).
  `macho32.py` = tiny Mach-O reader (modern nm/otool choke on the kext's LC_UNIXTHREAD); `layouts2.py` = vtable layouts.

## Plan (test each stage; link tests with `kextload -t` are safe, they do not load)
1. [done] old/new layout tables for kernel classes.
2. [done] measure whether one slot-translation rule fits all classes the kext uses (mostly yes; collisions between
   IOService-chain and OSObject-chain own-slots need receiver-type inference).
3. rewrite every kext class vtable into the new layout (needs family-class layouts: IOGraphicsFamily, IOPCIFamily, IONDRVSupport old+new).
4. patch virtual call sites (`call *disp(%reg)`), `this`-receiver calls exactly, others by slot-range + type heuristics.
5. `kextload -t` on the laptop until it links, then staged live load, then GA plugin / GL driver vs 10.4.8 OpenGL.framework.
Odds of full success: low; GL plugin ABI is a second unknown.
