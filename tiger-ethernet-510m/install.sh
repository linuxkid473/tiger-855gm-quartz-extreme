#!/bin/sh
# Install the Intel PRO/100 (8255x) driver that has an i386 slice into Mac OS X Tiger (10.4.8, JaS).
#
# Why: Tiger's own AppleIntel8255x binary is PowerPC-only ("Executable file doesn't contain kernel
# extension code"), so the Dell Inspiron 510m's Intel PRO/100 VE never gets an en0.  This kext is the
# one shipped in the JaS 10.4.8 AMD package, with a ppc + i386 binary and the same plist.
#
# Run on the laptop, in Terminal:
#     sudo sh /Volumes/BEOSPATCH/tiger-ethernet-510m/install.sh
#
SRC="$(cd "$(dirname "$0")" && pwd)/AppleIntel8255x.kext"
DST=/System/Library/Extensions/IONetworkingFamily.kext/Contents/PlugIns
BAK=/Library/Backup-AppleIntel8255x

[ "$(id -u)" = "0" ] || { echo "run with sudo"; exit 1; }
[ -d "$SRC" ] || { echo "missing $SRC"; exit 1; }
[ -d "$DST" ] || { echo "missing $DST - is this the right volume?"; exit 1; }

echo "== backing up the current kext to $BAK"
mkdir -p "$BAK"
if [ -d "$DST/AppleIntel8255x.kext" ]; then
    mv "$DST/AppleIntel8255x.kext" "$BAK/AppleIntel8255x.kext.$(date +%Y%m%d%H%M%S)"
fi

echo "== installing the i386-capable kext"
ditto "$SRC" "$DST/AppleIntel8255x.kext" || exit 1
chown -R root:wheel "$DST/AppleIntel8255x.kext"
find "$DST/AppleIntel8255x.kext" -type d -exec chmod 755 {} \;
find "$DST/AppleIntel8255x.kext" -type f -exec chmod 644 {} \;
chmod 755 "$DST/AppleIntel8255x.kext/Contents/MacOS/AppleIntel8255x"
lipo -info "$DST/AppleIntel8255x.kext/Contents/MacOS/AppleIntel8255x" 2>&1

echo "== loading it now"
kextload -v 2 "$DST/AppleIntel8255x.kext"

echo "== rebuilding the kernel cache so it loads at every boot"
touch /System/Library/Extensions
kextcache -k /System/Library/Extensions

echo "== result"
kextstat | grep -i 8255
ifconfig -a | grep -E '^en[0-9]'
echo "Done. If en0 is listed, open System Preferences > Network and add Built-in Ethernet (DHCP)."
