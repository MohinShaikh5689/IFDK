#!/usr/bin/env bash
# Downloads StikJIT.framework 1.9.0 (MPL-2.0, StikDebug/StikJIT tag 1.9.0, which bundles idevice
# under MIT) into $1/StikJIT.framework. The binary is the one willfaust/Madeira ships, pinned by
# commit and checked by SHA-256. Only the JIT helper extension links it.
set -euo pipefail

OUT="$1/StikJIT.framework"
BASE="https://raw.githubusercontent.com/willfaust/Madeira/279fd0f83170425db002fe25d6bd62f6355f497d/app/Frameworks/StikJIT.xcframework/ios-arm64/StikJIT.framework"
FILES="
f15ad5c6766fa07d545be283c1c99eccde6a8baa97d73978a09ec080ca6381df StikJIT
073c54a49b0bfd08131ae38fb470b56fe7a8757501663fbbae7b1d0e85a1f5df Headers/StikJIT-Swift.h
58c3cff9a8e501fc51d3cf65c2fca5c91be796668cdd61aaf0b428144cf92207 Modules/module.modulemap
695e75f6c6c021dbd0c23086767a3c659c7851fa0a6dbd6dbeec05e8e5ea3121 Modules/StikJIT.swiftmodule/arm64-apple-ios.swiftinterface
695e75f6c6c021dbd0c23086767a3c659c7851fa0a6dbd6dbeec05e8e5ea3121 Modules/StikJIT.swiftmodule/arm64-apple-ios.private.swiftinterface
d63b0675c3eeeebef0dbc597a77ba3ce8826af32ce201b533d64f2f3a0c8f0d4 Modules/StikJIT.swiftmodule/arm64-apple-ios.abi.json
753e7bba37913cd6c4e68b399735a8c9a7b8d475ba04d8b36ce1afd6678c9fbc universal.js
"
rm -rf "$OUT"
echo "$FILES" | while read -r sum path; do
  [ -n "$path" ] || continue
  mkdir -p "$(dirname "$OUT/$path")"
  curl -fsSL --retry 4 -o "$OUT/$path" "$BASE/$path"
  echo "$sum  $OUT/$path" | shasum -a 256 -c -
done
