#!/bin/bash
# Run once per GParted boot, in a terminal on the Dell:
#   sudo mkdir -p /mnt/d; mountpoint -q /mnt/d || sudo mount /dev/sdb2 /mnt/d; sudo bash /mnt/d/gem/ssh-up.sh
ssh-keygen -A >/dev/null 2>&1
echo "sshd: ALL" > /etc/hosts.allow; : > /etc/hosts.deny
systemctl restart ssh 2>/dev/null || service ssh restart
export DISPLAY=:0; xset s off; xset -dpms; xset s noblank 2>/dev/null
systemctl mask sleep.target suspend.target hibernate.target hybrid-sleep.target >/dev/null 2>&1
echo "ssh up on: $(hostname -I)"
