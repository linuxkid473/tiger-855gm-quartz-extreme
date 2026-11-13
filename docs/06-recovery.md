# Recovery procedures

A bad `AppleIntel830` kext (or a bad GL piece) can stop the machine at the login window or hard-freeze it. Plan the way back **before** installing.

## Rules

1. Keep the stable kext (p8 or the last known-good) next to the installed one, e.g. `/Library/AppleIntel830.p51`.
2. Test new kext binaries by `kextload` from a staging directory first; install into `/System/Library/Extensions` only after they ran.
3. Replace files through a **new inode** (`cp` to a temp name, `chown root:wheel`, `chmod 755`, `mv -f`). Never overwrite a mapped file in place.
4. `Extensions.mkext` caches kext binaries: after changing a kext run `sudo kextcache -k /System/Library/Extensions`; when recovering offline neutralise the cache (below).
5. The GPU cannot be reset by software. After a 3D stall only a power cycle helps; QE then needs the priming batch (p51 does that automatically).

## Switch QE off without touching the kext

```bash
# restore the original compositor config (keep a copy of the QE one)
CG=/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreGraphics.framework/Resources
sudo cp -p $CG/Configuration.plist.orig $CG/Configuration.plist
sudo killall WindowServer          # or reboot
```

(2D acceleration stays on; QE is "Not Supported" again.) Restoring the stock OpenGL pieces: keep a tar of `OpenGL.framework/Versions/A/{Resources,Libraries}` before replacing them.

## Boot hangs (login window never starts)

* Boot single-user / safe: `-x` or `-s` at the boot prompt; remove or replace the kext; `kextcache -k`.
* If you cannot get a shell: boot a Linux live stick (GParted Live works). Journaled HFS+ is mounted read-only by Linux, so **patch the file's bytes in place on the block device** using its physical blocks (`FIBMAP`; the replacement must be exactly the same size - all kext versions here are 225,310 bytes), **and** make Tiger ignore `Extensions.mkext` by changing the boot flags in `/Library/Preferences/SystemConfiguration/com.apple.Boot.plist` from `-v` to `-f` (same length) in place. In Tiger run `sudo kextcache -k /System/Library/Extensions` and set the flag back.
* A stalled-but-alive GPU shows in `system.log` as an `INSTDONE 037b10c1` dump and `lastReadStamp` frozen; the machine still answers over SSH (use Ethernet - a wedged WindowServer does not stop `sshd`).

## Things that froze the machine hard (do not repeat)

* Rewriting the GL batch from a kext hook (p22/p23/p27/p30).
* Calling `waitForTimeStamp` from inside `submit_buffer` (p41) - deadlock at login.
* Loading a kext that serialises submissions from `submit_commands` (p40).
* Writing code into zero runs at the end of the GA plug-in's `__TEXT` (they are string slots): WindowServer silently never initialises the accelerator.
