#!/bin/sh
# Run as root on the Dell from the directory that holds iwi.kext and this install/ folder:  sudo sh install/install.sh
set -e
HERE=`dirname "$0"`
[ "`id -u`" = 0 ] || { echo "run with sudo"; exit 1; }
mkdir -p /usr/local/sbin /Library/StartupItems /Library/Extensions
rm -rf /Library/Extensions/iwi.kext; cp -R "$HERE/../iwi.kext" /Library/Extensions/iwi.kext
chown -R root:wheel /Library/Extensions/iwi.kext
find /Library/Extensions/iwi.kext -type d -exec chmod 755 {} \;
find /Library/Extensions/iwi.kext -type f -exec chmod 644 {} \;
for f in iwi-connect iwi-setup iwi-status; do cp "$HERE/$f" /usr/local/sbin/$f; chown root:wheel /usr/local/sbin/$f; chmod 755 /usr/local/sbin/$f; done
rm -rf /Library/StartupItems/IWIWifi; mkdir /Library/StartupItems/IWIWifi
cp "$HERE/IWIWifi/IWIWifi" "$HERE/IWIWifi/StartupParameters.plist" /Library/StartupItems/IWIWifi/
chown -R root:wheel /Library/StartupItems/IWIWifi; chmod 755 /Library/StartupItems/IWIWifi /Library/StartupItems/IWIWifi/IWIWifi; chmod 644 /Library/StartupItems/IWIWifi/StartupParameters.plist
kextload -n -t /Library/Extensions/iwi.kext
echo "installed. Next:  sudo iwi-setup   (enter the Wi-Fi name and passphrase)"
