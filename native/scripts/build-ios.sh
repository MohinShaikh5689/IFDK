#!/usr/bin/env bash
# Builds FEXCore and gow3core for iOS arm64 and merges every static library they need into one
# archive the Expo module vendors: app/modules/gow3-core/ios/lib/libgow3core_all.a (+ headers).
# Needs macOS with Xcode, CMake and Ninja (GitHub Actions macos runners have them).
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
FEX="$ROOT/native/third_party/FEX"
OUT="${1:-$ROOT/build/native-ios}"
DEST="$ROOT/app/modules/gow3-core/ios/lib"

# Our fixes to the FEX fork (a submodule we cannot push to), applied once to its checkout.
for patch in "$ROOT"/native/patches/fex/*.patch; do
  if git -C "$FEX" apply --reverse --check "$patch" 2>/dev/null; then
    echo "FEX patch already applied: $(basename "$patch")"
  else
    git -C "$FEX" apply "$patch"
    echo "FEX patch applied: $(basename "$patch")"
  fi
done

cmake -S "$FEX" -B "$OUT" -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_SYSTEM_PROCESSOR=arm64 -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PROJECT_FEX_INCLUDE="$ROOT/native/cmake/fex_project.cmake" \
  -DBUILD_TESTING=OFF -DBUILD_THUNKS=OFF -DBUILD_FEXCONFIG=OFF -DBUILD_FEX_LINUX_TESTS=OFF \
  -DENABLE_FEX_ALLOCATOR=OFF -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF -DENABLE_ASSERTIONS=OFF \
  -DENABLE_CCACHE=ON -DENABLE_LTO=OFF -DENABLE_ZYDIS=OFF -DENABLE_OFFLINE_TELEMETRY=OFF \
  -DENABLE_GDB_SYMBOLS=OFF -DTUNE_CPU=none -DTUNE_ARCH=generic \
  -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON -DCMAKE_DISABLE_FIND_PACKAGE_unordered_dense=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_range-v3=ON
cmake --build "$OUT" --target gow3core gow3runtime JemallocLibs

# Every archive in the build tree (FEXCore, FEXCore_Base, CodeEmitter, fmt, xxhash, cephes,
# SoftFloat, ...), merged into one. JemallocDummy is built from the same source as JemallocLibs,
# so it is left out to keep one definition of each allocator hook.
mkdir -p "$DEST"
find "$OUT" -name '*.a' ! -name 'libJemallocDummy.a' | sort > "$OUT/libs.txt"
sed 's/^/  /' "$OUT/libs.txt"
xcrun libtool -static -no_warning_for_no_symbols -o "$DEST/libgow3core_all.a" $(cat "$OUT/libs.txt")
# Every symbol the C API and FEXCore need must resolve inside the archive (plus libc/libc++).
# Not `nm | grep -q`: grep exits at the first match and pipefail then fails on nm's SIGPIPE.
SYMBOLS=$(nm -gU "$DEST/libgow3core_all.a" 2>/dev/null || true)
for symbol in _gow3_selftest _gow3_start __ZN7FEXCore9Allocator6mallocEm ___clear_cache; do
  grep -q " T $symbol\$" <<<"$SYMBOLS" || { echo "missing $symbol in libgow3core_all.a" >&2; exit 1; }
done
mkdir -p "$DEST/include"
cp "$ROOT/native/include/gow3core.h" "$DEST/include/"
ls -la "$DEST"
