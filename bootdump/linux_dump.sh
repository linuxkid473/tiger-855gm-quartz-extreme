#!/bin/bash
# Dump 855GM registers from a running Linux (GParted Live shell) after i915 has initialised the chip.
# Output: /tmp/dump-linux.txt  (+ copy to the FAT partition labelled DUMP855 if found)
# Format: "PCI <bus:dev.fn> <offset> <dword>"  and  "MMIO <offset> <dword>"
GPU=/sys/bus/pci/devices/0000:00:02.0
OUT=/tmp/dump-linux.txt
: > "$OUT"

[ "$(cat $GPU/vendor 2>/dev/null)" = "0x8086" ] || { echo "No Intel GPU at 00:02.0"; exit 1; }
echo "# device $(cat $GPU/device)  driver: $(basename "$(readlink $GPU/driver 2>/dev/null)")" >> "$OUT"
uname -r >> "$OUT.kernel"; sed -i 's/^/# kernel /' "$OUT.kernel"; cat "$OUT.kernel" >> "$OUT"; rm -f "$OUT.kernel"

# PCI config (256 bytes) for host bridge, GPU function 0 and function 1
for d in 00:00.0 00:02.0 00:02.1; do
  f=/sys/bus/pci/devices/0000:$d/config
  [ -r "$f" ] || continue
  od -v -An -tx4 -w4 "$f" | awk -v d="$d" '{printf "PCI %s 0x%02x 0x%s\n", d, (NR-1)*4, $1}' >> "$OUT"
done

# MMIO BAR is BAR1 on the 855GM
base=$(awk 'NR==2{print $1}' $GPU/resource)
echo "# mmio base $base" >> "$OUT"
base=$((base))

rd() { # offset, 4K pages
  local off=$1 pages=$2
  if dd if=/dev/mem bs=4096 skip=$(( (base+off)/4096 )) count=$pages 2>/dev/null | od -v -An -tx4 -w4 \
       | awk -v o=$off '{printf "MMIO 0x%x 0x%s\n", o+(NR-1)*4, $1}' > /tmp/rd.part && [ -s /tmp/rd.part ]; then
    cat /tmp/rd.part >> "$OUT"
  else
    echo "read of 0x$(printf %x $off) via /dev/mem failed (try booting with iomem=relaxed)" >&2
  fi
}
rd 0x2000 1      # fence registers etc.
rd 0x6000 1      # clock gating, D_STATE
rd 0x61000 1     # display pipe/panel
rd 0x70000 2     # display plane/pipe config

echo "wrote $OUT ($(wc -l < "$OUT") lines)"
grep -c '^MMIO' "$OUT" | sed 's/^/MMIO lines: /'
for dev in $(ls /dev/disk/by-label 2>/dev/null | grep -i dump855); do
  mkdir -p /mnt/d && mount /dev/disk/by-label/$dev /mnt/d && cp "$OUT" /mnt/d/ && sync && umount /mnt/d \
    && echo "copied to DUMP855 partition"
done
