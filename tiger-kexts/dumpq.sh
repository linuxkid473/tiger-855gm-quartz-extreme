#!/bin/bash
# usage: dumpq.sh "{0x02028800,160}, {0x0202d020,80}"   -> builds, pushes, loads gpudump and prints the log
set -e
cd "$(dirname "$0")"
V=$(date +%H%M%S)
{ echo "#define RANGES $1"; cat src/gpudump.c; } > src/gpudump_q.c
./build.sh src/gpudump_q.c out/gpudump.raw.o
python3 -I fixmacho.py out/gpudump.raw.o out/gpudump.o
./mkkext.sh gpudump com.local.gpudump 0.1.0 out/gpudump.o out
./push.sh gpudump | tail -1
~/.ssh/tiger510m.sh 'sudo kextunload -b com.local.gpudump >/dev/null 2>&1; sudo kextload /Library/StagingWifi/gpudump.kext 2>&1 | tail -1; sleep 3; sudo dmesg | grep "gpudump:" | tail -400'
