#!/bin/bash
# Run on the Dell in GParted Live (as root, after the desktop is up): bash /mnt/d/gem/go.sh
# Order matters: blt (2D fill, checks the harness) -> solid (3D, no texture) -> tri / rect (3D, textured).
D=/mnt/d/gem
modprobe i915 2>/dev/null
dmesg | grep -i -E 'i915|drm' | tail -15 > $D/dmesg-before.txt
for t in blt solid tri rect; do
  echo "===== $t =====" | tee $D/last-started.txt
  sync
  python3 $D/gem.py $D/b_$t.txt
  sync
  dmesg | grep -i -E 'i915|drm|hang|reset' | tail -25 > $D/dmesg-after-$t.txt
  sync
done
echo "done; results are in $D"
