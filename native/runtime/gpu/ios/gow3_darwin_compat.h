// gow3 (iOS): Linux calls the GPU library uses, for Darwin. Force-included into every C++ source
// (native/cmake/gow3gpu.cmake), as shim/gow3_win_compat.h is on Windows.
#pragma once
#ifdef __APPLE__
#include <pthread.h>
#include <stdint.h>

// The calling thread's system-wide id, the value compat_gettid() returns in the runtime.
static inline uint64_t gettid(void) {
    uint64_t id = 0;
    pthread_threadid_np(nullptr, &id);
    return id;
}
#endif
