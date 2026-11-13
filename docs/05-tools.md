# Tools and test harnesses

## Python (host Mac, `abi-port/` and `tools/`)

| Script | Purpose |
|---|---|
| `abiport.py` | vtable rewrite + virtual-call translation DTK 10.4.1 -> retail 10.4.8 |
| `macho32.py`, `layouts2.py` | tiny Mach-O reader (modern `nm`/`otool` choke on the kext's `LC_UNIXTHREAD`), vtable layout extraction |
| `hfsx.py` | read-only HFS+ extractor for the DTK disk image |
| `mk_p44.py` ... `mk_p51.py` | kext patch generators (see [03](03-patch-reference.md)); each asserts the original bytes |
| `glfix.py`, `glfix2.py`, `gafix.py` | GL driver / GA plug-in patches |
| `tools/gen_patch_tables.py` | builds `PATCH_TABLES.md` from a chain of files |
| `tools/qtkit_force_gworld.py` | QuickTime video fix ([08](08-video-playback.md)) |
| `tools/mangle.pl`, `hide_ati.pl`, `ycchide.pl` | rename GL extension strings in `GLEngine` (experiments; not needed in the final config) |
| `tools/ext.rb` | print the renderer and extension list a normal app gets |

## Ruby GL test kit (`abi-port/gltest/`, runs on Tiger's stock Ruby via `DL`)

`ctx.rb` (create a hardware CGL context and probe it), `renderers.rb`, `limits.rb`, `tex.rb` (texture upload matrix per renderer), `draw.rb`, `ycc.rb`/`yccdraw.rb` (YCbCr texture tests), plus buffer/word dumpers. No compiler is needed on the target.

## Kernel extensions (`tiger-kexts/`)

Plain-C kexts built with clang (`-target i386-apple-darwin8 -mkernel`, `build.sh` / `fixmacho.py` / `mkkext.sh`; `tiger-kexts/stub/` has stand-in headers so the xnu tree is not required). Prebuilt tarballs of the diagnostic kexts are in `tiger-kexts/prebuilt/`.

| Kext | Purpose |
|---|---|
| `gttwatch` | thread that points unmapped GTT pages at a scratch page (the early proof of the invalid-PTE cause) |
| `gttfill` | one-shot GTT fill |
| `gpuregs`, `gpudump`, `gpupoke` | read/dump/poke chip registers and ring state |
| `mmiodiff` (+ table) | diff live MMIO against a Linux reference dump |
| `gpustress` (variants 0-4) | submits batches **through Apple's own `Intel830::submit_buffer`**: 0 no-op, 1 blit, 2 Linux-proven textured copy (the "priming" batch), 3/4 Apple's captured stalled batch. Uses the slot block that `mk_p45.py` hooks. |
| `iwi` | Intel 2200BG Wi-Fi driver for Tiger (needs the firmware you generate yourself) |

## Linux reference (`bootdump/`)

GParted Live (i686, non-PAE) with the i915 driver loaded: `gem/` has Mesa-i830-style and IGT gen2 batch emitters (`emit.c`, `emit_igt.c`), a ctypes ioctl runner and the batches; `dumps/` the cold-BIOS and Linux-initialised register dumps; a 16-bit real-mode bootable register dumper reads the same ranges before any OS runs.

## Debug tricks that worked

* `sudo ktrace -f out -p 1 -i -tcn` + `killall WindowServer` + `kdump` to see what WindowServer maps/unmaps.
* Load a bundle in Tiger's `python2.3` (`imp.load_dynamic`) to see dyld link errors.
* Copy Tiger binaries to the Mac and use `otool -arch i386 -tV` (PIC bases via `get_pc_thunk`).
* `sample <pid> 3` on Tiger shows exactly where a process burns CPU (this found the fragment-program emulator).
* SSH to Tiger needs legacy algorithms (`KexAlgorithms=+diffie-hellman-group1-sha1`, `HostKeyAlgorithms=+ssh-rsa`, `Ciphers=+aes128-cbc`); each login costs ~30% CPU on a Pentium M, so avoid chatty loops.
