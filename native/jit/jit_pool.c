/* SPDX-License-Identifier: GPL-3.0-or-later
 * See jit_pool.h. The prepare/detach calls follow the StikDebug universal JIT script's protocol
 * (the same one Madeira and MeloNX use); the RW view is a vm_remap of the debugger's RX pages. */
#include "jit_pool.h"

#include <errno.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <unistd.h>

#ifndef CS_OPS_STATUS
#define CS_OPS_STATUS 0
#endif
#ifndef CS_DEBUGGED
#define CS_DEBUGGED 0x10000000
#endif
extern int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);

#define PAGE ((size_t)16384)
#define MAX_FREE 1024
#define BRK_F00D 0xd43e01a0u

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t rx_base, rw_base;
static size_t pool_size, used;
typedef struct { size_t offset, size; } Range;
static Range free_ranges[MAX_FREE];
static size_t free_count;

uint32_t gow3_jit_cs_flags(void) {
    uint32_t flags = 0;
    return csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) == 0 ? flags : 0;
}
int gow3_jit_cs_debugged(void) { return (gow3_jit_cs_flags() & CS_DEBUGGED) != 0; }
int gow3_jit_traced(void) {
    struct kinfo_proc info;
    size_t len = sizeof(info);
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    memset(&info, 0, sizeof(info));
    return sysctl(mib, 4, &info, &len, NULL, 0) == 0 && (info.kp_proc.p_flag & P_TRACED) != 0;
}

static void trap(int sig, siginfo_t *info, void *context) {
    (void)info;
    ucontext_t *uc = (ucontext_t *)context;
    uint64_t pc = uc->uc_mcontext->__ss.__pc;
    if ((pc & 3) || *(const uint32_t *)(uintptr_t)pc != BRK_F00D) {
        signal(sig, SIG_DFL); /* not ours: trap again and crash with a report */
        return;
    }
    uc->uc_mcontext->__ss.__pc = pc + 4;
    uc->uc_mcontext->__ss.__x[0] = 0;
}
void gow3_jit_install_trap_fallback(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = trap;
    sigaction(SIGTRAP, &sa, NULL);
}

__attribute__((noinline, optnone)) void *gow3_jit_prepare_region(void *addr, size_t len) {
    register void *x0 __asm__("x0") = addr;
    register size_t x1 __asm__("x1") = len;
    __asm__ volatile("mov x16, #1\n"
                     "brk #0xf00d\n"
                     : "+r"(x0)
                     : "r"(x1)
                     : "x16", "memory");
    return x0;
}
__attribute__((noinline, optnone)) void gow3_jit_detach(void) {
    __asm__ volatile("mov x16, #0\n"
                     "brk #0xf00d\n" ::: "x16", "memory");
}

int gow3_jit_pool_create(size_t size) {
    size = (size + PAGE - 1) & ~(PAGE - 1);
    pthread_mutex_lock(&lock);
    if (rx_base) { pthread_mutex_unlock(&lock); return -1; }
    gow3_jit_install_trap_fallback();
    void *rx = gow3_jit_prepare_region(NULL, size);
    if (!rx) { pthread_mutex_unlock(&lock); return -2; }
    mach_port_t task = mach_task_self();
    vm_address_t rw = 0;
    vm_prot_t cur = 0, max = 0;
    if (vm_remap(task, &rw, size, 0, VM_FLAGS_ANYWHERE, task, (vm_address_t)rx, FALSE, &cur, &max, VM_INHERIT_NONE) != KERN_SUCCESS) {
        pthread_mutex_unlock(&lock);
        return -3;
    }
    if (vm_protect(task, rw, size, FALSE, VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS) {
        vm_deallocate(task, rw, size);
        pthread_mutex_unlock(&lock);
        return -4;
    }
    /* Both views must show the same bytes. */
    volatile uint32_t *probe_rw = (volatile uint32_t *)rw;
    probe_rw[0] = BRK_F00D;
    if (*(volatile uint32_t *)rx != BRK_F00D) {
        vm_deallocate(task, rw, size);
        pthread_mutex_unlock(&lock);
        return -5;
    }
    probe_rw[0] = 0;
    rx_base = (uintptr_t)rx;
    rw_base = (uintptr_t)rw;
    pool_size = size;
    free_ranges[0] = (Range){0, size};
    free_count = 1;
    used = 0;
    pthread_mutex_unlock(&lock);
    return 0;
}

int gow3_jit_pool_ready(void) { return rx_base != 0; }
uintptr_t gow3_jit_pool_rx(void) { return rx_base; }
size_t gow3_jit_pool_size(void) { return pool_size; }
int64_t gow3_jit_write_offset(void) { return rx_base ? (int64_t)(rw_base - rx_base) : 0; }
size_t gow3_jit_pool_used(void) { return used; }

int gow3_jit_in_pool(uintptr_t p, size_t size, int rw) {
    const uintptr_t base = rw ? rw_base : rx_base;
    return base && p >= base && p - base <= pool_size && size <= pool_size - (p - base);
}

void *gow3_jit_alloc(size_t size) {
    if (!size) return NULL;
    size = (size + PAGE - 1) & ~(PAGE - 1);
    pthread_mutex_lock(&lock);
    for (size_t i = 0; i < free_count; ++i) {
        if (free_ranges[i].size < size) continue;
        const size_t offset = free_ranges[i].offset;
        free_ranges[i].offset += size;
        free_ranges[i].size -= size;
        if (!free_ranges[i].size) {
            memmove(&free_ranges[i], &free_ranges[i + 1], (free_count - i - 1) * sizeof(Range));
            --free_count;
        }
        used += size;
        pthread_mutex_unlock(&lock);
        /* The emitter writes through the RW view, so the guard goes there; FEX protects the RX one. */
        mprotect((void *)(rw_base + offset + size - PAGE), PAGE, PROT_NONE);
        return (void *)(rx_base + offset);
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

void gow3_jit_free(void *rx, size_t size) {
    if (!rx || !size) return;
    size = (size + PAGE - 1) & ~(PAGE - 1);
    const size_t offset = (uintptr_t)rx - rx_base;
    mprotect((void *)(rw_base + offset), size, PROT_READ | PROT_WRITE);
    /* FEX left the last RX page inaccessible; a page that cannot be made executable again stays out. */
    if (mprotect(rx, size, PROT_READ | PROT_EXEC) != 0) {
        size -= PAGE;
        if (!size) return;
    }
    pthread_mutex_lock(&lock);
    size_t i = 0;
    while (i < free_count && free_ranges[i].offset < offset) ++i;
    if (free_count == MAX_FREE) { /* leak rather than corrupt the list */
        used -= size;
        pthread_mutex_unlock(&lock);
        return;
    }
    memmove(&free_ranges[i + 1], &free_ranges[i], (free_count - i) * sizeof(Range));
    free_ranges[i] = (Range){offset, size};
    ++free_count;
    used -= size;
    if (i + 1 < free_count && free_ranges[i].offset + free_ranges[i].size == free_ranges[i + 1].offset) {
        free_ranges[i].size += free_ranges[i + 1].size;
        memmove(&free_ranges[i + 1], &free_ranges[i + 2], (free_count - i - 2) * sizeof(Range));
        --free_count;
    }
    if (i > 0 && free_ranges[i - 1].offset + free_ranges[i - 1].size == free_ranges[i].offset) {
        free_ranges[i - 1].size += free_ranges[i].size;
        memmove(&free_ranges[i], &free_ranges[i + 1], (free_count - i - 1) * sizeof(Range));
        --free_count;
    }
    pthread_mutex_unlock(&lock);
}

void *gow3_jit_mmap_hook(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (!(prot & PROT_EXEC)) return mmap(addr, length, prot, flags, fd, offset);
    if (addr || fd != -1 || !rx_base) { errno = ENOMEM; return MAP_FAILED; }
    void *p = gow3_jit_alloc(length);
    if (!p) { errno = ENOMEM; return MAP_FAILED; }
    return p;
}

int gow3_jit_munmap_hook(void *addr, size_t length) {
    if (gow3_jit_in_pool((uintptr_t)addr, length, 0)) {
        gow3_jit_free(addr, length);
        return 0;
    }
    return munmap(addr, length);
}

int64_t gow3_jit_native_test(void) {
    static const uint32_t code[] = {0xd2800540u /* mov x0, #42 */, 0xd65f03c0u /* ret */};
    void *rx = gow3_jit_alloc(2 * PAGE);
    if (!rx) return -1;
    memcpy((void *)((uintptr_t)rx + gow3_jit_write_offset()), code, sizeof(code));
    sys_icache_invalidate(rx, sizeof(code));
    const int64_t result = ((int64_t (*)(void))rx)();
    gow3_jit_free(rx, 2 * PAGE);
    return result;
}
