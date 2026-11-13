#!/bin/bash
# runs bis/b_k000..kNNN in order, stops at the first one that does not finish ("wait ok"); prints the culprit command
D=/mnt/d/gem
for f in $(ls $D/bis/b_k*.txt | sort); do
  out=$(timeout 30 python3 $D/gem.py $f 2>&1)
  if echo "$out" | grep -q "wait ok"; then
    echo "$(basename $f): ok"
  else
    echo "$(basename $f): FAILED"; echo "$out" | head -3
    k=$(basename $f .txt | sed 's/b_k0*//'); [ -z "$k" ] && k=0
    echo "culprit = Apple ctx command #$k:"; grep "^$(printf %03d $k) " $D/bis/cmds.txt
    break
  fi
done
