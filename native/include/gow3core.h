/* SPDX-License-Identifier: GPL-3.0-or-later
 * The C API the app shell (Swift, modules/gow3-core) calls. Strings are JSON written into the
 * caller's buffer; the return value is the length needed (excluding the terminator). */
#ifndef GOW3CORE_H
#define GOW3CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Device, signing and memory facts: model, RAM, task address map, footprint, available memory. */
size_t gow3_device_report(char *out, size_t capacity);
/* CS_DEBUGGED is set (a debugger attached at some point). */
int gow3_jit_debugged(void);
/* A debugger (StikDebug) is attached right now. */
int gow3_jit_debugger_attached(void);
/* With the debugger attached: creates the JIT pool (`pool_mb` MiB), then detaches the debugger.
 * 0 on success, else the jit_pool.h error. */
int gow3_jit_setup(unsigned pool_mb);
/* Runs the self-tests (memory always, JIT and FEXCore once the pool exists), appending progress
 * lines to `log_path` as it goes, so a crash still leaves the last step on disk. */
size_t gow3_selftest(const char *log_path, char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
