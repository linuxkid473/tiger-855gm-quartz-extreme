#!/bin/bash
# Installs the GPU dumper boot code onto a USB stick (legacy BIOS boot). DESTROYS the stick's boot code and first
# 4 KB of data (partition table kept). usage: ./install_dumper.sh diskN
set -e
D="$1"; [ -n "$D" ] || { echo "usage: $0 diskN"; exit 1; }
diskutil info "$D" | grep -E "Device / Media Name|Total Size|Protocol|Removable|Device Location"
read -p "Overwrite boot code of /dev/$D with the dumper? type YES: " a; [ "$a" = YES ] || exit 1
HERE="$(cd "$(dirname "$0")" && pwd)"
diskutil unmountDisk force "$D" >/dev/null
sudo dd if=/dev/r$D of="$HERE/mbr.backup" bs=512 count=1 2>/dev/null
python3 - "$HERE" <<'PY'
import sys
h=sys.argv[1]
m=bytearray(open(h+'/mbr.backup','rb').read())
m[0:446]=open(h+'/stage1.bin','rb').read()
# make the last used partition entry active for BIOSes that insist on a bootable flag
for i in (2,1,0):
    if m[446+16*i+4]!=0: m[446+16*i]=0x80; break
m[510:512]=b'\x55\xaa'
open(h+'/mbr.new','wb').write(m)
PY
sudo dd if="$HERE/mbr.new" of=/dev/r$D bs=512 count=1 2>/dev/null
sudo dd if="$HERE/stage2.bin" of=/dev/r$D bs=512 seek=1 count=8 2>/dev/null
# zero the dump slot header so a stale dump is not mistaken for a new one
dd if=/dev/zero bs=512 count=32 2>/dev/null | sudo dd of=/dev/r$D bs=512 seek=1228800 2>/dev/null
sync; diskutil unmountDisk force "$D" >/dev/null || true
echo "done. Boot the Dell from this stick."
