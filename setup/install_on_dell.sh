#!/bin/sh
# Recreates the Dell Inspiron 510m (Intel 855GM, Tiger 10.4.8) graphics/CPU/Wi-Fi/video setup from this repo.
# Runs ON the Dell, as root.  Normally started by setup/recreate.sh, which unpacks the repo files into $1.
#   sudo sh install_on_dell.sh /tmp/tiger-setup
# Idempotent. Every file it replaces is first copied to /Library/Backups/tiger-setup/<name>.<md5>.
# Files whose current checksum is not a known base are left alone (a warning is printed).
S=${1:-/tmp/tiger-setup}
B=/Library/Backups/tiger-setup
EXT=/System/Library/Extensions
mkdir -p "$B"
say() { echo "[setup] $*"; }
warn() { echo "[setup] WARNING: $*"; }
md5of() { md5 -q "$1" 2>/dev/null || md5 "$1" | sed 's/.*= //'; }
backup() { [ -f "$1" ] || return 0; m=`md5of "$1"`; n=`basename "$1"`; [ -f "$B/$n.$m" ] || cp -p "$1" "$B/$n.$m"; }
# put SRC DST [MODE]: back up DST, then replace it through a new inode (the kernel caches the old one)
put() { src="$1"; dst="$2"; mode="${3:-755}"; [ -f "$src" ] || { warn "missing $src"; return 1; }
        backup "$dst"; cp "$src" "$dst.new" && chmod $mode "$dst.new" && chown root:wheel "$dst.new" && mv -f "$dst.new" "$dst"; }
# known `current` checksums: replace only when DST is one of them (or already the target)
isknown() { cur=`md5of "$1"`; shift; for k in "$@"; do [ "$cur" = "$k" ] && return 0; done; return 1; }
[ "`id -u`" = 0 ] || { echo "run as root (sudo)"; exit 1; }
[ -d "$S/binaries" ] || { echo "no $S/binaries: run setup/recreate.sh from the repo"; exit 1; }

# 1. AppleIntel830 kext (graphics kernel driver): the stable p54t (p54 with a 100x longer GPU wait timeout)
K=$EXT/AppleIntel830.kext/Contents/MacOS/AppleIntel830
if [ ! -f "$K" ]; then
    say "no AppleIntel830.kext installed: installing the p51 bundle from the repo first"
    cp -R "$S/binaries/AppleIntel830.kext" $EXT/ && chown -R root:wheel $EXT/AppleIntel830.kext
fi
put "$S/binaries/variants/AppleIntel830.p54t" "$K" 755 && say "AppleIntel830 kext = p54t (`md5of $K`)"
for v in p8 p45b p48b p50b p52 p53 p54 p54t p55 p55f p56 p56f; do [ -f "$S/binaries/variants/AppleIntel830.$v" ] && cp "$S/binaries/variants/AppleIntel830.$v" /Library/AppleIntel830.$v; done
[ -f "$S/binaries/AppleIntel830.kext/Contents/MacOS/AppleIntel830" ] && cp "$S/binaries/AppleIntel830.kext/Contents/MacOS/AppleIntel830" /Library/AppleIntel830.p51
mkdir -p /usr/local/sbin /usr/local/bin
put "$S/tools/system/gfx-mode" /usr/local/sbin/gfx-mode 755

# 2. GL driver: DTK driver, HOME-variant (apps use the software renderer; WindowServer keeps the hardware QE compositor) with
#    both GL_APPLE_ycbcr_422 table records set to the 855GM's SWAPY mode (hardware YUV->RGB)
G=$EXT/AppleIntel830GLDriver.bundle/Contents/MacOS/AppleIntel830GLDriver
if [ -f "$G" ]; then
    if isknown "$G" 069b3315e9c64ddeb7b516710c25305f 7c3263bde5943a81ccfe9cd65433aaa7 9d2bca1c834ccad52a05e484e774ad86 b8005166f412b8fcec27a0cbf192e941; then
        put "$S/binaries/apple-derived/AppleIntel830GLDriver.home-variant.ycc" "$G" 755 && say "GL driver = home-variant + ycbcr fix"
    else warn "GL driver has an unknown checksum (`md5of $G`): not replaced"; fi
else warn "no AppleIntel830GLDriver.bundle: install it from your DTK copy first (abi-port/glfix*.py), then re-run"; fi

# 3. Patched GLEngine for the opt-in hardware-GL stack (/Library/OGL1041, a path-rewritten 10.4.1 OpenGL.framework copy)
E=/Library/OGL1041/OpenGL.framework/Versions/A/Resources/GLEngine.bundle/GLEngine
if [ -f "$E" ]; then
    if isknown "$E" 8af6c56bfdb8c9f566585a4ee8a983fb 91ff01fdd0f7500d810b300ab4e4b1a3; then
        put "$S/binaries/apple-derived/GLEngine.1041.ycc" "$E" 755 && say "GLEngine (OGL1041) patched"
    else warn "OGL1041 GLEngine unknown checksum: not replaced"; fi
else say "no /Library/OGL1041 (optional hardware-GL stack, see docs/11): skipping GLEngine"; fi

# 4. QTKit: force the QuickDraw ("GWorld") video path (no Core Image fragment-program emulation)
Q=/System/Library/Frameworks/QTKit.framework/Versions/A/QTKit
if [ -f "$Q" ]; then
    if isknown "$Q" eb4931e6b4db2c7da60318273b26c441 32fdc01f2bbd75eab151b543066ef0c2; then
        put "$S/binaries/apple-derived/QTKit.gworld-patched" "$Q" 755 && say "QTKit patched"
    else warn "QTKit unknown checksum (`md5of $Q`): not replaced"; fi
fi

# 5. VLC 0.9.10 macosx plug-in: ask for "no recovery" instead of "accelerated" (7 bytes at 0x50cd2; works with or without hardware GL)
V=/Applications/VLC.app/Contents/MacOS/modules/libmacosx_plugin.dylib
if [ -f "$V" ]; then
    b=`od -An -tx1 -j 330962 -N 7 "$V" | tr -d ' \n'`
    case "$b" in
      c745a449000000) backup "$V"; cp "$V" "$V.new" && printf 'H' | dd of="$V.new" bs=1 seek=330965 conv=notrunc 2>/dev/null && chmod 755 "$V.new" && mv -f "$V.new" "$V" && say "VLC plug-in patched";;
      c745a448000000) say "VLC plug-in already patched";;
      *) warn "VLC plug-in bytes at 0x50cd2 not as expected ($b): not patched";;
    esac
fi
put "$S/tools/system/vlc-hw" /usr/local/bin/vlc-hw 755 && say "vlc-hw installed (EXPERIMENTAL: hardware-GL VLC can lock this GPU up, see docs/16)"

# 6. CPU pin (the Pentium M drops to 600 MHz about every 69 s with no thermal throttle) + boot loader with a crash guard
rm -rf /Library/Extensions/cpupin.kext && cp -R "$S/binaries/cpupin/cpupin.kext" /Library/Extensions/ && chown -R root:wheel /Library/Extensions/cpupin.kext
mkdir -p /Library/StartupItems/CpuPin
put "$S/tools/cpupin/CpuPin" /Library/StartupItems/CpuPin/CpuPin 755
put "$S/tools/cpupin/CpuPin.plist" /Library/StartupItems/CpuPin/StartupParameters.plist 644
say "cpupin kext + StartupItem installed"

# 7. Wi-Fi start-up script v4 (only when the iwi driver and StartupItem are already there; the iwi kext embeds Intel firmware and is not in the repo)
if [ -f /usr/local/sbin/iwi-connect ]; then
    put "$S/tools/wifi/iwi-connect" /usr/local/sbin/iwi-connect 755 && say "iwi-connect v4"
else say "no iwi Wi-Fi setup found: skipping (build iwi.kext per tiger-kexts/, then re-run)"; fi

# 8. rebuild the kext cache (the Dell clock is unreliable: force a newer timestamp on the directory)
touch $EXT && kextcache -l -m /System/Library/Extensions.mkext $EXT >/dev/null 2>&1 && say "kext cache rebuilt"
say "done. Reboot to apply (kexts and the GL driver load at boot).  Check: gfx-mode status; kextstat | grep cpupin"
