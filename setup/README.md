# Recreate the setup (Dell Inspiron 510m, Intel 855GM, Tiger 10.4.8)

```bash
./setup/recreate.sh vihaannathan@192.168.0.235 ~/.ssh/id_rsa_tiger     # then reboot the Dell
```
That packs `binaries/` + `tools/`, sends them over ssh (Tiger's OpenSSH 4 needs legacy algorithms, already set) and runs `setup/install_on_dell.sh` with sudo on the Dell. Every file it replaces is first copied to `/Library/Backups/tiger-setup/<name>.<md5>`; files with an unknown checksum are left alone and reported.

## What it installs
| # | What | Source in the repo | Notes |
|---|---|---|---|
| 1 | `AppleIntel830.kext` = **p54t** (installs the p51 bundle first if no kext is present); stages all variants as `/Library/AppleIntel830.<name>`; `gfx-mode` | `binaries/variants/`, `binaries/AppleIntel830.kext`, `tools/system/gfx-mode` | `gfx-mode stable` = p54t. Do **not** use p55/p56 with hardware GL. |
| 2 | GL driver (HOME-variant + both YCbCr records SWAPY) | `binaries/apple-derived/AppleIntel830GLDriver.home-variant.ycc` | needs `AppleIntel830GLDriver.bundle` already installed (your own DTK copy, see abi-port). Rebuildable: `tools/ycc_hw.py`. |
| 3 | Opt-in hardware-GL stack: patched `GLEngine` (private driver dir `/Library/OGL1041/Ext`) + a private copy of the GL driver with the depth/stencil readback fix | `binaries/apple-derived/GLEngine.1041.ycc-ext`, `AppleIntel830GLDriver.depthstencil` (rebuild: `tools/gldriver_depthstencil.py`) | only if `/Library/OGL1041` exists (docs/11, docs/18) |
| 4 | QTKit forced onto the QuickDraw video path | `binaries/apple-derived/QTKit.gworld-patched` | only if the current QTKit is the stock or already-patched 7.1.3 |
| 5 | VLC 0.9.10 plug-in patch (7 bytes) and `vlc-hw` launcher | patch applied in place; `tools/system/vlc-hw` | `vlc-hw` is experimental, it can lock the GPU up (docs/16) |
| 6 | `cpupin` kext + StartupItem (Pentium M stays at 1.6 GHz) | `binaries/cpupin/cpupin.kext`, `tools/cpupin/` | with a crash guard |
| 7 | Wi-Fi start-up script v4 | `tools/wifi/iwi-connect` | only if the iwi Wi-Fi setup already exists (the kext embeds Intel firmware and is not in the repo: `tiger-kexts/`) |
| 8 | system version spoof: `SystemVersion.plist` reports **10.4.11** (build 8S165); original backed up | |
| 9 | rebuilds the kext cache | | the Dell clock is wrong, the script forces a newer timestamp |
| 10 | `hangguard` kext + StartupItem: resets the machine when the GPU ring is stuck 20 s | `binaries/hangguard/`, `tools/hangguard/` | the ICH4 hardware watchdog does not work here (docs/18); enable **Automatic login** or the Dell waits at the login window after a reset |
| 11 | GL launchers `glhw`/`glsw`; iTunes and Chess wrapped to start on the hardware GL stack | `tools/system/` | only if `/Library/OGL1041` exists (docs/18) |

## What it does NOT include (you must have these first)
Apple's DTK `AppleIntel830GLDriver.bundle`, the 10.4.1 `OpenGL.framework` copy at `/Library/OGL1041` (optional), the iwi Wi-Fi kext and its firmware, the Ethernet kext, the boot loader / kernel (the author uses a `semthex` 8.8.1 kernel). See the top-level README, docs/11 and docs/12.

## Stable configuration in one sentence
QuickTime for video (re-encode with `tools/encode_tiger_video.sh`), normal VLC 0.9.10 for everything else, `p54t` + `cpupin`; avoid any player output that uses the GPU for video (see docs/17).
