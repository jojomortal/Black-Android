#!/bin/sh
# usage: build-ios.sh <variant: none|eva|iml|both> [codesign identity] [provisioning profile]
# without an identity the bundle is left unsigned (compile check only)
set -e
cd "$(dirname "$0")"
V=${1:-none}; ID=$2; PROF=$3
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
APP=build/$V/Memtest.app
rm -rf "$APP"; mkdir -p "$APP"
CF="-arch arm64 -isysroot $SDK -miphoneos-version-min=16.0 -O1"
xcrun clang++ $CF -std=c++17 -c spike.cpp -o build/$V/spike.o
xcrun clang $CF -fobjc-arc -c main.m -o build/$V/main.o
xcrun clang++ $CF build/$V/spike.o build/$V/main.o -framework UIKit -framework Foundation -o "$APP/Memtest"
cat > "$APP/Info.plist" <<P
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>Memtest</string>
<key>CFBundleIdentifier</key><string>${BUNDLE_ID:-com.example.blackmemtest}</string>
<key>CFBundleName</key><string>Memtest</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string>
<key>CFBundleShortVersionString</key><string>1</string>
<key>MemtestVariant</key><string>$V</string>
<key>UIFileSharingEnabled</key><true/>
<key>UILaunchScreen</key><dict/>
<key>MinimumOSVersion</key><string>16.0</string>
<key>UIDeviceFamily</key><array><integer>1</integer></array>
</dict></plist>
P
if [ -n "$ID" ]; then
    [ -n "$PROF" ] && cp "$PROF" "$APP/embedded.mobileprovision"
    codesign -f -s "$ID" --entitlements ent/$V.plist "$APP"
    echo "signed $APP with entitlements: $V"
else
    echo "built unsigned $APP"
fi
