// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: the runtime's write provenance trace (GOW3_WRITE_TRACE=1) for the GPU side. The record
// kinds mirror runtime.h (WTRACE_*), which the GPU library does not include (its compat headers).
#pragma once

#include <cstdint>

extern "C" {
extern int runtime_wtrace_enabled;
void runtime_wtrace(unsigned kind, std::uintptr_t address, std::uint64_t size, std::uint64_t a,
                    std::uint64_t b);
}

namespace Gow3WriteTrace {
enum : unsigned {
    Backing = 9,     ///< runtime.h WTRACE_BACKING
    DmaCopyCpu = 10, ///< WTRACE_DMA_COPY_CPU
    DmaCopyGpu = 11, ///< WTRACE_DMA_COPY_GPU
    DmaFill = 12,    ///< WTRACE_DMA_FILL
};
} // namespace Gow3WriteTrace
