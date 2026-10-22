#!/usr/bin/env bash
# Builds FEXCore and gow3core for iOS arm64 and merges every static library they need into one
# archive the Expo module vendors: app/modules/gow3-core/ios/lib/libgow3core_all.a (+ headers).
# Needs macOS with Xcode, CMake and Ninja (GitHub Actions macos runners have them).
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
FEX="$ROOT/native/third_party/FEX"
OUT="${1:-$ROOT/build/native-ios}"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd) # absolute: CMake rejects relative include paths (the MoltenVK headers)
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

# MoltenVK (Apache-2.0): Vulkan on Metal, the static iOS library and its Vulkan headers. It goes under
# $OUT, so the archive merge below picks up libMoltenVK.a with the rest.
MOLTENVK_VERSION=v1.4.2
MOLTENVK_SHA256=b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1
MOLTENVK="$OUT/moltenvk"
if [ ! -f "$MOLTENVK/MoltenVK/MoltenVK/static/MoltenVK.xcframework/ios-arm64/libMoltenVK.a" ]; then
  mkdir -p "$MOLTENVK"
  curl -fsSL -o "$MOLTENVK/MoltenVK-ios.tar" \
    "https://github.com/KhronosGroup/MoltenVK/releases/download/$MOLTENVK_VERSION/MoltenVK-ios.tar"
  echo "$MOLTENVK_SHA256  $MOLTENVK/MoltenVK-ios.tar" | shasum -a 256 -c -
  tar -xf "$MOLTENVK/MoltenVK-ios.tar" -C "$MOLTENVK"
  rm "$MOLTENVK/MoltenVK-ios.tar"
fi

# SDL3 (Zlib): the GPU library's overlay uses its event types, and SDL audio/gamepads come later. A
# static library without SDL_main: UIKit belongs to the React Native shell, SDL video stays unused.
SDL3_VERSION=3.2.24
SDL3_SHA256=81cc0fc17e5bf2c1754eeca9af9c47a76789ac5efdd165b3b91cbbe4b90bfb76
SDL3="$OUT/sdl3/SDL3-$SDL3_VERSION"
if [ ! -f "$SDL3/include/SDL3/SDL.h" ]; then
  mkdir -p "$OUT/sdl3"
  curl -fsSL -o "$OUT/sdl3/SDL3.tar.gz" \
    "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz"
  echo "$SDL3_SHA256  $OUT/sdl3/SDL3.tar.gz" | shasum -a 256 -c -
  tar -xzf "$OUT/sdl3/SDL3.tar.gz" -C "$OUT/sdl3"
  rm "$OUT/sdl3/SDL3.tar.gz"
fi
if [ "${GOW3_BUILD:-}" != gpu-check ] && [ ! -f "$OUT/sdl3-build/libSDL3.a" ]; then
  cmake -S "$SDL3" -B "$OUT/sdl3-build" -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_BUILD_TYPE=Release \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF
  cmake --build "$OUT/sdl3-build" --target SDL3-static
fi
# Boost (header-only parts: container, icl, intrusive, pool, asio) from Homebrew, like glslang.
BOOST_INCLUDE="${GOW3_BOOST_INCLUDE:-$(brew --prefix boost)/include}"

cmake -S "$FEX" -B "$OUT" -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_SYSTEM_PROCESSOR=arm64 -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PROJECT_FEX_INCLUDE="$ROOT/native/cmake/fex_project.cmake" \
  -DGOW3_MOLTENVK_INCLUDE="$MOLTENVK/MoltenVK/MoltenVK/include" \
  -DGOW3_SDL3_INCLUDE="$SDL3/include" -DGOW3_BOOST_INCLUDE="$BOOST_INCLUDE" \
  -DBUILD_TESTING=OFF -DBUILD_THUNKS=OFF -DBUILD_FEXCONFIG=OFF -DBUILD_FEX_LINUX_TESTS=OFF \
  -DENABLE_FEX_ALLOCATOR=OFF -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF -DENABLE_ASSERTIONS=OFF \
  -DENABLE_CCACHE=ON -DENABLE_LTO=OFF -DENABLE_ZYDIS=OFF -DENABLE_OFFLINE_TELEMETRY=OFF \
  -DENABLE_GDB_SYMBOLS=OFF -DTUNE_CPU=none -DTUNE_ARCH=generic \
  -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON -DCMAKE_DISABLE_FIND_PACKAGE_unordered_dense=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_range-v3=ON
# gpu-check: compile the GPU library only, continuing past errors so that one CI run lists them all.
if [ "${GOW3_BUILD:-}" = gpu-check ]; then
  cmake --build "$OUT" --target gow3gpu -- -k 0
  exit
fi
cmake --build "$OUT" --target gow3core gow3runtime gow3gpu JemallocLibs

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
for symbol in _gow3_selftest _gow3_start _gow3_gpu_screen_test _vkCreateInstance _vkCreateMetalSurfaceEXT \
    _gow3gpu_init _gow3_gpu_set_surface _SDL_Delay \
    __ZN7FEXCore9Allocator6mallocEm ___clear_cache; do
  grep -q " T $symbol\$" <<<"$SYMBOLS" || { echo "missing $symbol in libgow3core_all.a" >&2; exit 1; }
done
mkdir -p "$DEST/include"
cp "$ROOT/native/include/gow3core.h" "$DEST/include/"
ls -la "$DEST"
