#!/bin/bash
# generates src/fw_bss.h from the Intel iwi-bss firmware image (kept out of git: Intel's licence)
set -e
FW=${1:-$HOME/Desktop/zeta/ZetaWifi/fw-private/x/firmware/iwi-bss}
HERE="$(cd "$(dirname "$0")" && pwd)"
{
  echo "static const unsigned char iwi_fw[] = {"
  xxd -i < "$FW"
  echo "};"
  echo "static const unsigned int iwi_fw_len = $(stat -f %z "$FW");"
} > "$HERE/src/fw_bss.h"
