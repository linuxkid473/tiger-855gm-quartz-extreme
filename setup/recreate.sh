#!/bin/bash
# One line to recreate the 855GM Tiger setup on the Dell from this repo (run on the Mac that has the repo):
#     ./setup/recreate.sh [user@]HOST [path/to/ssh/key]
# e.g. ./setup/recreate.sh vihaannathan@192.168.0.235 ~/.ssh/id_rsa_tiger
# It packs the binaries and tools, sends them over ssh (legacy algorithms for Tiger's OpenSSH 4) and runs
# setup/install_on_dell.sh with sudo on the Dell (you are asked for the Dell's password once if sudo needs it).
# Then reboot the Dell.  See setup/README.md for what it installs and what it expects to be there already.
set -e
HOST=${1:?usage: recreate.sh [user@]HOST [ssh-key]}
KEY=${2:-$HOME/.ssh/id_rsa_tiger}
R="$(cd "$(dirname "$0")/.." && pwd)"
OPTS=(-o StrictHostKeyChecking=no
      -o KexAlgorithms=+diffie-hellman-group1-sha1,diffie-hellman-group14-sha1,diffie-hellman-group-exchange-sha1
      -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa -o Ciphers=+aes128-cbc,aes256-cbc,3des-cbc -o MACs=+hmac-sha1)
[ -f "$KEY" ] && OPTS+=(-i "$KEY" -o IdentitiesOnly=yes)
cd "$R"
echo "packing $(du -sh binaries | cut -f1) of binaries + tools ..."
COPYFILE_DISABLE=1 tar cf - binaries tools/system tools/wifi tools/cpupin/CpuPin tools/cpupin/CpuPin.plist tools/hangguard/HangGuard tools/hangguard/HangGuard.plist setup/install_on_dell.sh \
  | ssh -T "${OPTS[@]}" "$HOST" 'rm -rf /tmp/tiger-setup && mkdir /tmp/tiger-setup && cd /tmp/tiger-setup && tar xf - && echo unpacked'
ssh -t "${OPTS[@]}" "$HOST" 'sudo sh /tmp/tiger-setup/setup/install_on_dell.sh /tmp/tiger-setup'
echo "done: reboot the Dell now ->  ssh ... 'sudo shutdown -r now'"
