#!/usr/bin/env bash
# Builds the JIT helper extension (native/jit-helper) and installs it into a built app:
#   <app>/PlugIns/Gow3JITHelper.appex   (bundle ID <app id>.JITHelper, so sideloaders rename it with the app)
#   <app>/Frameworks/StikJIT.framework  (the helper loads it via @executable_path/../../Frameworks)
# Usage: build-jit-helper.sh <dir holding StikJIT.framework> <path to .app>. Needs macOS with Xcode.
# Signing is left to the packaging step (inner bundles first).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FW_DIR="$(cd "$1" && pwd)"
APP="$2"
PB=/usr/libexec/PlistBuddy
APP_ID=$($PB -c 'Print :CFBundleIdentifier' "$APP/Info.plist")
VERSION=$($PB -c 'Print :CFBundleShortVersionString' "$APP/Info.plist")
BUILD=$($PB -c 'Print :CFBundleVersion' "$APP/Info.plist")

APPEX="$APP/PlugIns/Gow3JITHelper.appex"
rm -rf "$APPEX"
mkdir -p "$APPEX"
xcrun -sdk iphoneos swiftc -O -swift-version 5 -parse-as-library -application-extension \
  -target arm64-apple-ios26.0 -module-name Gow3JITHelper \
  -F "$FW_DIR" -framework StikJIT \
  -Xlinker -e -Xlinker _NSExtensionMain \
  -Xlinker -rpath -Xlinker @executable_path/../../Frameworks \
  "$ROOT/native/jit-helper/Gow3JITHelper.swift" "$ROOT/app/modules/gow3-core/ios/Gow3JITRequest.swift" \
  -o "$APPEX/Gow3JITHelper"
sed -e "s/__BUNDLE_ID__/$APP_ID.JITHelper/" -e "s/__VERSION__/$VERSION/" -e "s/__BUILD__/$BUILD/" \
  "$ROOT/native/jit-helper/Info.plist" > "$APPEX/Info.plist"
plutil -convert binary1 "$APPEX/Info.plist"

# StikJIT ships without an Info.plist, which an embedded framework needs.
DST="$APP/Frameworks/StikJIT.framework"
rm -rf "$DST"
mkdir -p "$APP/Frameworks"
ditto "$FW_DIR/StikJIT.framework" "$DST"
rm -rf "$DST/Headers" "$DST/Modules"
P="$DST/Info.plist"
$PB -c 'Add :CFBundleExecutable string StikJIT' -c "Add :CFBundleIdentifier string $APP_ID.StikJIT" \
  -c 'Add :CFBundleName string StikJIT' -c 'Add :CFBundlePackageType string FMWK' \
  -c 'Add :CFBundleInfoDictionaryVersion string 6.0' -c 'Add :CFBundleShortVersionString string 1.9.0' \
  -c 'Add :CFBundleVersion string 1' -c 'Add :CFBundleSupportedPlatforms array' \
  -c 'Add :CFBundleSupportedPlatforms:0 string iPhoneOS' -c 'Add :MinimumOSVersion string 17.4' "$P" >/dev/null
plutil -convert binary1 "$P"
echo "-> $APPEX ($APP_ID.JITHelper), $DST"
