#!/bin/bash
# usage: mkkext.sh name bundle-id version obj.o outdir   -> outdir/name.kext (and outdir/name.tar for sending to the Dell)
set -e
N=$1; ID=$2; VER=$3; OBJ=$4; OUT=$5
K="$OUT/$N.kext"; rm -rf "$K"; mkdir -p "$K/Contents/MacOS"
cp "$OBJ" "$K/Contents/MacOS/$N"
cat > "$K/Contents/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key><string>English</string>
	<key>CFBundleExecutable</key><string>$N</string>
	<key>CFBundleIdentifier</key><string>$ID</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key><string>$N</string>
	<key>CFBundlePackageType</key><string>KEXT</string>
	<key>CFBundleSignature</key><string>????</string>
	<key>CFBundleVersion</key><string>$VER</string>
	<key>OSBundleLibraries</key>
	<dict>
		<key>com.apple.kpi.bsd</key><string>8.0.0b2</string>
		<key>com.apple.kpi.iokit</key><string>8.0.0b2</string>
		<key>com.apple.kpi.libkern</key><string>8.0.0b2</string>
		<key>com.apple.kpi.mach</key><string>8.0.0b2</string>
		<key>com.apple.kpi.unsupported</key><string>8.0.0b2</string>
	</dict>
</dict>
</plist>
PL
( cd "$OUT" && COPYFILE_DISABLE=1 tar cf "$N.tar" "$N.kext" )
