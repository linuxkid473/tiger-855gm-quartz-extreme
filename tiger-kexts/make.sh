#!/bin/bash
# builds out/iwi.kext + out/iwi.tar from src/
set -e
cd "$(dirname "$0")"
[ -f src/fw_bss.h ] || ./mkfw.sh
./build.sh src/iwi_tiger.c out/iwi.raw.o
python3 -I fixmacho.py out/iwi.raw.o out/iwi.o
xcrun nm -arch i386 -u out/iwi.o | awk '{print $NF}' | while read s; do grep -q " $s\$" ${KSYMS:-/tmp/xnu792/ksyms.txt} || { echo "UNRESOLVED $s"; exit 1; }; done
./mkkext.sh iwi com.local.iwi 0.1.0 out/iwi.o out
ls -l out/iwi.tar
