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
/* Starts the game loader (native/runtime probe.c) on a thread of its own with the command line
 * argv[0..argc) (argv[1] is boot-linked.bin); its output is appended to `log_path`. The JIT pool must
 * be ready. 0 when the thread started. Errors after that end the process, as the PC build does. */
int gow3_start(int argc, const char *const *argv, const char *log_path);
/* Has every thread write its guest and host frames to the log (for a hang). Threads signalled, or -1. */
int gow3_dump_threads(void);
/* The game's window: the CAMetalLayer the GPU library presents to (width x height pixels, the screen's
 * maximum refresh rate). Call before the game opens its video output, and again when the size changes. */
void gow3_gpu_set_surface(void *metal_layer, uint32_t width, uint32_t height, uint32_t refresh_hz);
/* MoltenVK on the screen: a Vulkan swapchain on `metal_layer` (a CAMetalLayer, width x height pixels)
 * cleared to a cycling colour for `seconds`. JSON: device, driver, frames and fps, and which of the
 * extensions and features shadPS4's video core uses the device lacks. */
size_t gow3_gpu_screen_test(void *metal_layer, uint32_t width, uint32_t height, unsigned seconds, char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
