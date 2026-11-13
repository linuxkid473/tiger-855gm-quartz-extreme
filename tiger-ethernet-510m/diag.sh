#!/bin/sh
# Full network diagnostic for the Dell Inspiron 510m on Tiger.  Writes ONE file next to this script:
#     diag-out.txt
# Run on the laptop:
#     sudo sh /Volumes/<stick>/tiger-ethernet-510m/diag.sh
# then eject the stick and plug it into the Mac.  Changes nothing except reloading the 8255x kext.

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/diag-out.txt"
K=/System/Library/Extensions/IONetworkingFamily.kext/Contents/PlugIns/AppleIntel8255x.kext

[ "$(id -u)" = "0" ] || { echo "run with sudo"; exit 1; }

sec() { echo; echo "################ $1"; }

{
sec "date / system"
date; uname -a; sw_vers
sysctl hw.model hw.machine hw.physmem hw.cpufrequency 2>&1

sec "kext on disk"
ls -laR "$K" 2>&1 | head -40
lipo -info "$K/Contents/MacOS/AppleIntel8255x" 2>&1
kextload -t "$K" 2>&1
ls -la /Library/Backup-AppleIntel8255x 2>&1

sec "kextstat (network / 8255x)"
kextstat 2>&1 | grep -iE "8255|8254|Network|PCI|BCM|Ethernet"

sec "ifconfig -a"
ifconfig -a 2>&1

sec "networksetup"
networksetup -listallnetworkservices 2>&1
networksetup -listnetworkserviceorder 2>&1

sec "ioreg: the Ethernet PCI node (DCKS) with all properties"
ioreg -n DCKS -l -w0 2>&1 | head -80

sec "ioreg: Intel82557 / ethernet classes"
ioreg -c Intel82557 -l -w0 2>&1 | head -60
ioreg -c IOEthernetInterface -l -w0 2>&1 | head -40
ioreg -c IOEthernetController -l -w0 2>&1 | head -40

sec "ioreg: PCI devices (id summary)"
ioreg -c IOPCIDevice -w0 -l 2>&1 | grep -E '\+-o|"vendor-id"|"device-id"|"class-code"|"IOName"|"name"|"assigned-addresses"|"reg"' | head -150

sec "dmesg BEFORE reload (network-related)"
dmesg 2>&1 | grep -iE "8255|82557|PHY|ethernet|network|DCKS|PCI|kext|mbuf" | tail -40

sec "reload test: unload"
kextunload "$K" 2>&1
sleep 2
LINES=`dmesg 2>&1 | wc -l`

sec "reload test: load verbose"
kextload -v 6 "$K" 2>&1 | tail -15
sleep 5

sec "dmesg produced by the reload"
dmesg 2>&1 | sed -n "$((LINES+1)),\$p"

sec "ifconfig -a AFTER reload"
ifconfig -a 2>&1 | grep -E '^[a-z]+[0-9]+:'
ioreg -c Intel82557 -l -w0 2>&1 | head -30

sec "system.log (kernel / kext / network, last 120)"
grep -iE "kernel|kextd|8255|82557|ethernet|en0|DHCP|configd" /var/log/system.log 2>&1 | tail -120

sec "end"
date
} > "$OUT" 2>&1

sync
echo "Wrote $OUT ($(wc -l < "$OUT") lines). Eject the stick: diskutil eject \"$HERE\""
