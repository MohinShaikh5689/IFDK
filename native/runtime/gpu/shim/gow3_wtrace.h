// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: the runtime's write provenance trace (GOW3_WRITE_TRACE=1) for the GPU side. The record
// kinds mirror runtime.h (WTRACE_*), which the GPU library does not include (its compat headers).
#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
extern int runtime_wtrace_enabled;
void runtime_wtrace(unsigned kind, std::uintptr_t address, std::uint64_t size, std::uint64_t a,
                    std::uint64_t b);
// Tiling check (GOW3_TILE_CHECK=1, runtime_tilecheck.c): 1 when a tiling started at `address`, 2
// when it lies inside one that started elsewhere (info: that tiling), 0 when none covers it.
extern int runtime_tilecheck_enabled;
int runtime_tilecheck_classify(std::uintptr_t address, char* info, std::size_t size);
// Searches guest memory for copies of a texture's T# and logs them (a thread of its own).
void runtime_tilecheck_find_descriptor(const std::uint32_t tsharp[8], std::uintptr_t texture);
}

namespace Gow3WriteTrace {
enum : unsigned {
    Backing = 9,     ///< runtime.h WTRACE_BACKING
    DmaCopyCpu = 10, ///< WTRACE_DMA_COPY_CPU
    DmaCopyGpu = 11, ///< WTRACE_DMA_COPY_GPU
    DmaFill = 12,    ///< WTRACE_DMA_FILL
    GpuBufferWrite = 14, ///< WTRACE_GPU_BUFFER_WRITE
    GpuImageWrite = 15,  ///< WTRACE_GPU_IMAGE_WRITE
};
} // namespace Gow3WriteTrace
