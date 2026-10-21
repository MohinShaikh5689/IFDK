#!/usr/bin/env bash
# Builds native/rppairing (on-device remote pairing for the built-in JIT helper, iOS 27+) for iOS
# arm64 into app/modules/gow3-core/ios/lib/libgow3_rppairing.a, which the podspec vendors.
# Needs rustup; crates come from crates.io at the versions in native/rppairing/Cargo.lock.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TARGET=aarch64-apple-ios
export IPHONEOS_DEPLOYMENT_TARGET=17.0

rustup target add "$TARGET"
cd "$ROOT/native/rppairing"
cargo build --release --locked --target "$TARGET"
DEST="$ROOT/app/modules/gow3-core/ios/lib"
mkdir -p "$DEST"
cp "target/$TARGET/release/libgow3_rppairing.a" "$DEST/libgow3_rppairing.a"
# Not `nm | grep -q`: grep exits at the first match and pipefail then fails on nm's SIGPIPE.
SYMBOLS=$(nm -gU "$DEST/libgow3_rppairing.a" 2>/dev/null || true)
grep -q ' T _gow3_rppairing_accept$' <<<"$SYMBOLS"
echo "-> $DEST/libgow3_rppairing.a"
