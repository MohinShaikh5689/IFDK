#!/usr/bin/env bash
# Compiles the gow3 runtime sources for iOS arm64 (compile only, -Werror) and lists every failure.
# The SDL3 (audio, pad) and FFmpeg (mp3, video) parts wait for those libraries' iOS builds; probe.c
# (the loader's main) waits for the in-app entry point and the GPU library.
set -uo pipefail
cd "$(dirname "$0")"
OUT=${OUT:-/tmp/gow3-runtime-ios}
mkdir -p "$OUT"
SKIP=" runtime_audio.c runtime_pad.c runtime_mp3.c runtime_videodec.c "
failed=()
for source in src/runtime*.c src/compat_darwin.c; do
  name=$(basename "$source")
  [[ $SKIP == *" $name "* ]] && continue
  if xcrun -sdk iphoneos clang -target arm64-apple-ios17.0 -std=c11 -O2 -Wall -Wextra -Werror -pthread \
       -Isrc -c "$source" -o "$OUT/${name%.c}.o" 2> "$OUT/${name%.c}.log"; then
    echo "ok      $name"
  else
    echo "FAILED  $name"; sed 's/^/        /' "$OUT/${name%.c}.log" | head -40
    failed+=("$name")
  fi
done
[[ ${#failed[@]} -eq 0 ]] || { echo "${#failed[@]} failed: ${failed[*]}"; exit 1; }
