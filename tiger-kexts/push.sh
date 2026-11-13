#!/bin/bash
# usage: push.sh name   -> sends out/name.tar to the Dell, unpacks to /Library/StagingWifi/name.kext with correct ownership, validates it
set -e
N=$1
cat "$(dirname "$0")/out/$N.tar" | timeout 120 ~/.ssh/tiger510m.sh "cat > /tmp/$N.tar; sudo mkdir -p /Library/StagingWifi && cd /Library/StagingWifi && sudo rm -rf $N.kext && sudo tar xf /tmp/$N.tar 2>/dev/null; sudo chown -R root:wheel $N.kext && sudo find $N.kext -type d -exec chmod 755 {} \; && sudo find $N.kext -type f -exec chmod 644 {} \; && sudo kextload -n -t /Library/StagingWifi/$N.kext 2>&1 | tail -3"
