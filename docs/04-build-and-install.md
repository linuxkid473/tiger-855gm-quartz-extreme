# Build and install guide

**You need your own legal copies** of Apple's DTK 10.4.1 files; none are in this repository. Read [06-recovery.md](06-recovery.md) first.

## Inputs

| Input | Where from | Used for |
|---|---|---|
| `AppleIntel830.kext` (executable), `AppleIntel830GA.plugin`, `AppleIntel830GLDriver.bundle` | Tiger x86 DTK 10.4.1 image (HFS+; `abi-port/hfsx.py` is a small HFS+ reader/extractor since modern macOS cannot mount it) | patch targets |
| 10.4.1 `GLEngine`, `GLRendererFloat`, `GLRaster*`, `GLDriver` bundles, `libGLImage.dylib`, `libGLProgrammability.dylib` | same image, `OpenGL.framework` | the matching GL pieces |
| 10.4.1 kernel + `mach.sym`, `IO*Family` | same image | vtable layouts (old) |
| 10.4.8 kernel + families | your running Tiger 10.4.8 | vtable layouts (new) |
| `QTKit` binary | your Tiger 10.4.8 | video fix |

Layout used by the scripts (not committed): `abi-port/work/in/old/`, `work/in/new/`, `work/out/`.

## 1. Build the kext chain

```bash
cd abi-port
python3 abiport.py vtables          # rewrite vtables -> work/out/AppleIntel830.vt
python3 abiport.py calls            # translate call sites -> work/out/AppleIntel830.new  (= the p1 baseline of the tables)
# apply the p1->p8 byte edits from PATCH_TABLES.md (file offset / old bytes / new bytes; each stage lists its md5)
python3 mk_p45.py work/out/AppleIntel830.p8   work/out/AppleIntel830.p45b
python3 mk_p48.py work/out/AppleIntel830.p45b work/out/AppleIntel830.p48b
python3 mk_p50.py work/out/AppleIntel830.p48b work/out/AppleIntel830.p50n nocache
python3 mk_p51.py work/out/AppleIntel830.p50n work/out/AppleIntel830.p52
```

Every script asserts the exact original bytes before patching; if an assertion fails your input differs from the one used here. Checksums of the author's chain are at the top of [PATCH_TABLES.md](../PATCH_TABLES.md) (they match only for byte-identical inputs). Use `python3 tools/gen_patch_tables.py` to diff your own chain.

## 2. Install on the Tiger machine (always keep the p8 kext as fallback)

```bash
# new inode, never overwrite the running file in place
D=/System/Library/Extensions/AppleIntel830.kext/Contents/MacOS
sudo cp AppleIntel830.p52 $D/AppleIntel830.new
sudo chown root:wheel $D/AppleIntel830.new && sudo chmod 755 $D/AppleIntel830.new
sudo mv -f $D/AppleIntel830.new $D/AppleIntel830
sudo kextcache -k /System/Library/Extensions
```

Test a new kext **without installing** by loading it from a staging folder: `sudo kextload -l -m /Library/Staging830/AppleIntel830.kext` (a bad kext then costs one reboot, not a boot loop). Never put an untested kext into `/System/Library/Extensions`: it is cached in `Extensions.mkext` and a hang at boot needs the recovery procedure.

## 3. GL pieces and QE switch

1. Install the **10.4.1** `GLEngine`, `GLRendererFloat`, `GLRaster*`, `GLDriver` bundles and `libGLImage`/`libGLProgrammability` into `/System/Library/Frameworks/OpenGL.framework/Versions/A/{Resources,Libraries}` (backup first).
2. Use the **pristine** DTK `AppleIntel830GLDriver` (optionally with the `HOME` name patch - see [08](08-video-playback.md)).
3. In `CoreGraphics.framework/Resources/Configuration.plist` empty `GLCompositorRequiredClasses` (keep a `.orig`).
4. `killall WindowServer` (or reboot). System Profiler should now list *Quartz Extreme: Supported*.

## 4. Video fix

```bash
python3 tools/qtkit_force_gworld.py QTKit.stock QTKit.gworld   # see docs/08
```

## 5. Verify

* `kextstat | grep Intel830`; `dmesg`/`system.log`: `Accel caps: 00000001`.
* The Intel830 accelerator statistics (`ioreg -l`): `lastReadStamp` close behind `submitStamp`.
* Soak: drag windows around (the author used 9,000+ GPU jobs of window wiggling), then a **cold** power-on - the priming patch exists for exactly that.
