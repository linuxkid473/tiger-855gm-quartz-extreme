# Patched kernel extension binaries

**These are modified copies of Apple's `AppleIntel830` kext** (from the Mac OS X 10.4.1 developer-transition-kit image), ABI-translated to the 10.4.8 C++ layout and patched by the scripts in `abi-port/`. They are Apple's copyrighted code in derived form: they are included for convenience and for the author's own machine, with no warranty, and carry no licence from this repository. If you are not comfortable redistributing them, delete this directory and rebuild from your own copy of the DTK image ([docs/04-build-and-install.md](../docs/04-build-and-install.md)).

| File | md5 | Notes |
|---|---|---|
| `AppleIntel830.kext/` | `05526065323cbfcf8dbf8b748677d1d8` (executable) | **p51 - recommended.** 2D accel + QE; verified from a cold power-on. Install per the guide (`kextcache`, new inode). |
| `variants/AppleIntel830.p56f` | `71e024aebc65134bed80af84155cf2a3` | **Currently installed**: p56 without the vsync wait (fast). `p56` = with vsync. `p55`/`p55f` = no flush at all (superseded). Generators `mk_p55.py`, `mk_p56.py`. |
| `variants/AppleIntel830.p54` | `988258e0c102c31e1b0514d0c3109d0f` | p53 + spin/yield GPU waits (smoother). Generator: `abi-port/mk_p54.py`. Currently the author's installed kext. |
| `variants/AppleIntel830.p53` | `9000d051b0f65c1db3e20b447acbf411` | p51 + vblank-synced swap (tear-free). Generator: `abi-port/mk_p53.py`. Short soak only. |
| `variants/AppleIntel830.p8` | `c33b692b6d4335f3f915d9f6667751ee` | Stable 2D-acceleration-only base (pipe-B fix). Use as the recovery kext. |
| `variants/AppleIntel830.p45b` | `97cbb1f6d4f9466aeef232a826a0636d` | p8 + capture hook for the `gpustress` harness. |
| `variants/AppleIntel830.p48b` | `c3d66a2181fae8a1814d7ddaa03eab9b` | + GTT fill + Linux chip registers. |
| `variants/AppleIntel830.p50b` | `df102a41ec3aaf1641bcda155f091a05` | + READ_FLUSH + per-submit `wbinvd`. Still stalls from a cold boot. |
| `variants/AppleIntel830.p52` | `3cc492781873c9df86f0ae86d1ab63c4` | p51 without `wbinvd`; not verified from a cold boot. |

Only the executable differs between versions; the `Info.plist` is the DTK one (`com.apple.driver.AppleIntel830`). The kext matches PCI `8086:3582` only (855GM). The GL driver, GA plug-in and `OpenGL.framework` pieces are **not** included; build them with `glfix*.py` / `gafix.py` from your own DTK copy.

Not included: `iwi.kext` (the Wi-Fi driver) because it embeds Intel's firmware, which has its own licence - build it with `tiger-kexts/mkfw.sh`.

## Other patched binaries

**`apple-derived/`** - modified copies of Apple's Mac OS X Tiger / 10.4.1 DTK binaries (same notice as above: Apple's code, derived form, personal repo, no warranty):

| File | md5 | What |
|---|---|---|
| `QTKit.gworld-patched` | `32fdc01f2bbd75eab151b543066ef0c2` | QTKit 7.1.3 with two 9-byte getter patches (force the QuickDraw "GWorld" path; i386 slice). Stock md5 `eb4931e6b4db2c7da60318273b26c441`. Generator: `tools/qtkit_force_gworld.py`. |
| `AppleIntel830GLDriver.home-variant` | `7c3263bde5943a81ccfe9cd65433aaa7` | DTK GL driver whose `GL_REJECT_HW` switch name is renamed to `HOME`: every normal app gets the software renderer, WindowServer keeps the hardware QE compositor (the safe default). The pristine driver is the opt-in hardware one. |
| `AppleIntel830GA.fixed` | `e8a89e08f92d4a48a398359789be736c` | GA plug-in answering the `cgls`/`BGRA` capability queries (`abi-port/gafix.py`). NOT what is installed on the author's machine at the moment: the original plug-in is (it also plays video fine, and 640x384 clips freeze the machine with either). |

**`vlc/libmacosx_plugin.dylib.noaccel`** (`23c93ba563cc71c148486040c8ab7784`) - VLC 0.9.10 (Intel) macosx plug-in with the "accelerated pixel format required" request removed (`tools/vlc_noaccel.py`). Plays on software GL, but only at ~1 fps (see [docs/14](../docs/14-cache-flush-video-vlc-wifi.md)).

Video files: use `tools/encode_tiger_video.sh` (Baseline, 360 lines, native frame rate).
