#!/usr/bin/env bash
# The submodules the iOS native build needs (shallow). FEX's test-binary repositories are large and
# not needed; only what its configure uses with tests, thunks and the profiler off is fetched.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
git submodule update --init --depth 1 native/third_party/FEX native/runtime/third_party/LibAtrac9 \
  native/third_party/VulkanMemoryAllocator native/third_party/robin-map native/third_party/Vulkan-Headers \
  native/runtime/gpu/third_party/imgui native/runtime/gpu/third_party/magic_enum native/runtime/gpu/third_party/miniz
cd native/third_party/FEX
git submodule update --init --depth 1 External/fmt External/xxhash External/range-v3 External/unordered_dense \
  External/vixl
