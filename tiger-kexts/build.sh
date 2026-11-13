#!/bin/bash
# usage: build.sh src.c out.o   -- compiles one translation unit as a Tiger (10.4, i386) kernel extension object
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
X=${XNU:-/tmp/xnu792/xnu-xnu-792.13.8}
RES=$(clang -print-resource-dir)/include
clang -target i386-apple-darwin8 -c "$1" -o "$2" -O1 -mkernel -fno-builtin -fno-stack-protector -fno-common -fno-pic -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdinc -w \
  -DKERNEL -DAPPLE -DNeXT -D__APPLE_API_UNSTABLE \
  -I"$HERE/stub" -I"$HERE" -I"$HERE/src" -I$X/bsd -I$X/osfmk -I$X/libkern -I$X/iokit -I$X/EXTERNAL_HEADERS -isystem "$RES"
