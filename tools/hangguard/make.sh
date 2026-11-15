#!/bin/bash
# builds out/hangguard.kext (+ .tar) and out/hgprobe.kext (log-only). Needs /tmp/xnu792 (headers + ksyms.txt).
set -e
cd "$(dirname "$0")"
W=../tiger-wifi-2200bg
mkdir -p out
build() { # name srcfile bundleid
  $W/build.sh "$2" out/$1.raw.o
  python3 -I $W/fixmacho.py out/$1.raw.o out/$1.o
  xcrun nm -arch i386 -u out/$1.o | awk '{print $NF}' | while read s; do grep -q " $s\$" ${KSYMS:-/tmp/xnu792/ksyms.txt} || { echo "UNRESOLVED $s"; exit 1; }; done
  $W/mkkext.sh $1 $3 1.0.0 out/$1.o out
}
( printf '#define ARM 0\n'; sed 's/com.local.hangguard/com.local.hgprobe/' hangguard.c ) > out/probe.c
build hgprobe out/probe.c com.local.hgprobe
( printf '#define NOKICK 1\n#define TRACE 1\n'; sed 's/com.local.hangguard/com.local.hgtest/' hangguard.c ) > out/test.c
build hgtest out/test.c com.local.hgtest
build hangguard hangguard.c com.local.hangguard
ls -l out/*.tar
