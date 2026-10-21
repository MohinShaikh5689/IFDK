#!/usr/bin/env bash
# Runtime tests that run on Linux (x86-64 or arm64). Builds into ${OUT:-/tmp/gow3-runtime-tests}.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${OUT:-/tmp/gow3-runtime-tests}
mkdir -p "$OUT"
CC=${CC:-cc}
$CC -std=c11 -O2 -g -Wall -Wextra -Werror -DGOW3_WINDOW_LAYOUT -pthread -Isrc \
  src/runtime_memory.c tests/test_memory_window.c -o "$OUT/test_memory_window"
"$OUT/test_memory_window"
$CC -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -Isrc src/compat_darwin.c tests/test_compat_darwin.c \
  -o "$OUT/test_compat_darwin"
"$OUT/test_compat_darwin"
# The upstream (non-window) layout still builds.
$CC -std=c11 -O2 -Wall -Wextra -Werror -pthread -Isrc -c src/runtime_memory.c -o "$OUT/runtime_memory_upstream.o"
