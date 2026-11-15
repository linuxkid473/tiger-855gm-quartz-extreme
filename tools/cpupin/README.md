# cpupin - keep the Pentium M at its top SpeedStep state (Tiger 10.4.8, Dell Inspiron 510m)

`cpupin.c` is a plain-C kext (built with the Wi-Fi kext toolchain, `./make.sh`; needs `/tmp/xnu792` headers and `ksyms.txt` from the Dell's `/mach_kernel`).
Every 100 ms it reads IA32_PERF_STATUS (0x198) and IA32_THERM_STATUS (0x19c). If the CPU is not in the top state (`0x1028` = 1600 MHz, taken from this machine's ACPI _PSS table as seen in `ioreg`) and the CPU's own thermal throttle (therm status bit 0) is not active, it writes 0x1028 to IA32_PERF_CTL (0x199). It never writes while the hardware thermal throttle is active.
Logging (system.log, `kernel: cpupin:`): every state change, and a 60 s summary (`checks / changes / fixes / thermal-active`).

Install: `/Library/Extensions/cpupin.kext` + `/Library/StartupItems/CpuPin/` (`CpuPin`, `StartupParameters.plist`). The StartupItem has a crash guard (`/var/db/.cpupin.starting`): if a boot never finished loading the kext, the next boot skips it once.
Check: `grep cpupin /var/log/system.log`. Unload: `sudo kextunload -b com.local.cpupin`.
`hw.cpufrequency` always reports 1.59 GHz and says nothing about the real speed; use the log.
