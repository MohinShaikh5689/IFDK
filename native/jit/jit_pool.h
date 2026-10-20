/* SPDX-License-Identifier: GPL-3.0-or-later
 * The JIT pool: one dual-mapped region the debugger prepares once (iOS 26+ only lets a debugger
 * make pages executable), carved into code buffers for FEXCore. Code runs from the RX view and is
 * written through the RW view at rx + gow3_jit_write_offset(). */
#ifndef GOW3_JIT_POOL_H
#define GOW3_JIT_POOL_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif

/* CS_DEBUGGED is set: a debugger attached at some point (it stays set after detaching). */
int gow3_jit_cs_debugged(void);
/* A debugger is attached right now (P_TRACED). */
int gow3_jit_traced(void);
/* Code-signing status flags (csops CS_OPS_STATUS), or 0. */
uint32_t gow3_jit_cs_flags(void);
/* Skips an unanswered `brk #0xf00d` (x0 = 0) instead of crashing; other traps crash as usual.
 * An attached debugger still sees the BRK first. Safe to call more than once. */
void gow3_jit_install_trap_fallback(void);

/* StikDebug "universal" JIT protocol: x16 = 1 asks the debugger to allocate and prepare `len`
 * bytes of RX memory (addr NULL) and returns it, or NULL; x16 = 0 makes it detach. */
void *gow3_jit_prepare_region(void *addr, size_t len);
void gow3_jit_detach(void);

/* Creates the pool (needs the debugger attached): 0, or a negative step that failed
 * (-1 already created, -2 debugger gave no memory, -3 vm_remap, -4 vm_protect,
 * -5 the RW/RX coherence test). */
int gow3_jit_pool_create(size_t size);
int gow3_jit_pool_ready(void);
uintptr_t gow3_jit_pool_rx(void);
size_t gow3_jit_pool_size(void);
int64_t gow3_jit_write_offset(void);
size_t gow3_jit_pool_used(void);

/* RX carve of at least `size` bytes (16 KiB granularity), or NULL when the pool is full. Its last
 * page is inaccessible in the RW view, so an overrunning emitter faults (FEX's JIT guard page). */
void *gow3_jit_alloc(size_t size);
void gow3_jit_free(void *rx, size_t size);
/* [p, p + size) lies in the RX view or (rw = 1) the RW view of the pool. */
int gow3_jit_in_pool(uintptr_t p, size_t size, int rw);

/* FEXCore::Allocator hooks: executable mappings come from the pool, everything else from mmap. */
void *gow3_jit_mmap_hook(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int gow3_jit_munmap_hook(void *addr, size_t length);

/* Runs `mov x0, #42; ret` written through the RW view from the RX view; returns 42 when JIT works. */
int64_t gow3_jit_native_test(void);

#ifdef __cplusplus
}
#endif
#endif
