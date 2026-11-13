Tiger 10.4.8 (JaS) Ethernet for the Dell Inspiron 510m (Intel PRO/100 VE, PCI 8086:103D)

AppleIntel8255x.kext = the ppc+i386 build from the JaS 10.4.8 AMD package (Tiger's own one is PowerPC-only),
with ONE patch in the i386 code: Intel82557::_selfTest() now returns success (movl $1,%eax; ret).
Reason: the chip never answers the PORT self-test, so every result bit stays set and the driver aborts with
"Self test reports invalid ROM contents / internal register failure / serial subsystem failure".
The self-test is not needed (Linux e100 does not run it either).  Patched offset: i386 slice __text+0x45a2.

install.sh  : sudo sh install.sh      (backs up the old kext, installs, fixes permissions, loads, rebuilds cache)
diag.sh     : sudo sh diag.sh         (writes diag-out.txt next to it: kernel log, ioreg, reload test)
