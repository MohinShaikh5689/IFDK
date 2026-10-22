/* SPDX-License-Identifier: GPL-3.0-or-later
 * The loader inside the iOS app: probe.c's main (gow3_probe_main on Apple) on a thread of its own,
 * with its output appended to a log file, since an app has no terminal and cannot start processes. */
#ifndef __APPLE__ /* strdup on glibc; on Darwin it would hide pthread_from_mach_thread_np */
#define _POSIX_C_SOURCE 200809L
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#ifdef __APPLE__
#include <dlfcn.h>
#include <mach/mach.h>
#endif

int gow3_probe_main(int argc, char **argv);

static int loader_argc;
static char **loader_argv;
static pthread_key_t loader_key;
#ifdef __APPLE__
static mach_port_t loader_port = MACH_PORT_NULL; /* the dump reads its registers without a signal */
#endif

/* Runs only when the loader thread ends some other way than returning (pthread_exit in a guest call). */
static void loader_gone(void *unused) {
    (void)unused;
    static const char line[] = "Loader thread exited without returning (pthread_exit)\n";
    ssize_t written = write(2, line, sizeof(line) - 1); (void)written;
}

static void *loader(void *unused) {
    (void)unused;
#ifdef __APPLE__
    loader_port = mach_thread_self();
#endif
    pthread_setspecific(loader_key, (void *)1);
    int result = gow3_probe_main(loader_argc, loader_argv);
    pthread_setspecific(loader_key, NULL);
    /* The game normally never returns; the loader's own errors end the process through exit(). */
    printf("Loader returned %d\n", result);
    fflush(NULL);
    return NULL;
}

/* SDL3 (audio and gamepads in runtime_audio.c / runtime_pad.c) is a library here: UIKit belongs to the
 * React Native shell, so there is no SDL_main to mark SDL ready. */
void SDL_SetMainReady(void);

/* Starts the loader with argv[0..argc) (argv[0] is the program name, argv[1] the boot.bin path);
 * stdout and stderr go to log_path from here on. 0 when the thread started. Call once. */
int gow3_start(int argc, const char *const *argv, const char *log_path) {
    if (loader_argv || argc < 2) return -1;
    if (log_path && *log_path) {
        if (!freopen(log_path, "a", stdout) || !freopen(log_path, "a", stderr)) return -2;
        setvbuf(stderr, NULL, _IONBF, 0);
    }
    SDL_SetMainReady();
    /* SDL's HIDAPI controller drivers open CoreBluetooth, which iOS ends the app for without a
     * Bluetooth usage description; DualSense, DS4 and Xbox pads work through GameController. */
    setenv("SDL_JOYSTICK_HIDAPI", "0", 0);
    loader_argv = calloc((size_t)argc + 1, sizeof(char *));
    if (!loader_argv) return -3;
    for (int i = 0; i < argc; ++i) loader_argv[i] = strdup(argv[i]);
    loader_argc = argc;
    pthread_key_create(&loader_key, loader_gone);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20); /* host frames of guest calls nest deeply */
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    int e = pthread_create(&thread, &attr, loader, NULL);
    pthread_attr_destroy(&attr);
    return e ? -4 : 0;
}

#ifdef __APPLE__
static void log_address(const char *label, uintptr_t address) {
    Dl_info where;
    if (dladdr((void *)address, &where) && where.dli_sname)
        fprintf(stderr, "    %s %s+0x%lx\n", label, where.dli_sname, (unsigned long)(address - (uintptr_t)where.dli_saddr));
    else
        fprintf(stderr, "    %s %p (no symbol: JIT code or stripped)\n", label, (void *)address);
}

/* The loader thread's pc, lr and frame-pointer chain, read through Mach while it is suspended,
 * so it shows up even when it cannot run a signal handler. Symbols are looked up after it resumes,
 * since it may hold the dyld or stdio lock. */
static void dump_loader_registers(thread_act_t thread) {
#if defined(__arm64__) || defined(__aarch64__)
    enum { DEPTH = 32 };
    uintptr_t frames[DEPTH + 2];
    int depth = 0;
    if (thread_suspend(thread) != KERN_SUCCESS) { fprintf(stderr, "    (cannot suspend the loader thread)\n"); return; }
    arm_thread_state64_t state;
    mach_msg_type_number_t n = ARM_THREAD_STATE64_COUNT;
    if (thread_get_state(thread, ARM_THREAD_STATE64, (thread_state_t)&state, &n) == KERN_SUCCESS) {
        frames[depth++] = (uintptr_t)arm_thread_state64_get_pc(state);
        frames[depth++] = (uintptr_t)arm_thread_state64_get_lr(state);
        uintptr_t fp = (uintptr_t)arm_thread_state64_get_fp(state);
        while (fp && depth < DEPTH + 2) {
            uintptr_t pair[2];
            vm_size_t got = 0;
            if (vm_read_overwrite(mach_task_self(), fp, sizeof(pair), (vm_address_t)pair, &got) != KERN_SUCCESS || got != sizeof(pair)) break;
            frames[depth++] = pair[1];
            if (pair[0] <= fp) break;
            fp = pair[0];
        }
    }
    thread_resume(thread);
    if (depth == 0) { fprintf(stderr, "    (cannot read the loader thread's registers)\n"); return; }
    log_address("pc", frames[0]);
    log_address("lr", frames[1]);
    for (int i = 2; i < depth; ++i) {
        char label[8];
        snprintf(label, sizeof(label), "#%d", i - 2);
        log_address(label, frames[i]);
    }
#else
    (void)thread;
#endif
}
#endif

/* Every other thread writes where it is (guest frames, host symbols) to the log: SIGUSR2 runs
 * probe.c's thread_dump. Each Mach thread is listed first with its name, run state and whether
 * the signal went out, and the loader thread also gets its registers read directly.
 * For a game that hangs. The number of threads signalled, or -1. */
int gow3_dump_threads(void) {
    if (!loader_argv) return -1;
    fprintf(stderr, "--- thread dump requested\n");
#ifdef __APPLE__
    thread_act_array_t threads;
    mach_msg_type_number_t count = 0;
    mach_port_t self = mach_thread_self();
    int signalled = 0, loader_seen = 0;
    if (task_threads(mach_task_self(), &threads, &count) == KERN_SUCCESS) {
        fprintf(stderr, "--- %u Mach threads\n", (unsigned)count);
        for (mach_msg_type_number_t i = 0; i < count; ++i) {
            thread_extended_info_data_t info;
            mach_msg_type_number_t n = THREAD_EXTENDED_INFO_COUNT;
            int have_info = thread_info(threads[i], THREAD_EXTENDED_INFO, (thread_info_t)&info, &n) == KERN_SUCCESS;
            int is_loader = threads[i] == loader_port;
            pthread_t thread = threads[i] != self ? pthread_from_mach_thread_np(threads[i]) : NULL;
            int e = threads[i] == self ? 0 : thread ? pthread_kill(thread, SIGUSR2) : ESRCH;
            fprintf(stderr, "  mach thread 0x%x '%s'%s%s run_state=%d signal=%s\n", (unsigned)threads[i],
                    have_info ? info.pth_name : "?", is_loader ? " [loader]" : "", threads[i] == self ? " [self]" : "",
                    have_info ? info.pth_run_state : -1,
                    threads[i] == self ? "skipped" : !thread ? "no pthread" : e ? strerror(e) : "sent");
            if (thread && !e) { ++signalled; usleep(30000); }
            if (is_loader) { loader_seen = 1; dump_loader_registers(threads[i]); }
            mach_port_deallocate(mach_task_self(), threads[i]);
        }
        if (!loader_seen) fprintf(stderr, "--- the loader thread is gone (not among the Mach threads)\n");
        vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(*threads));
    }
    mach_port_deallocate(mach_task_self(), self);
    fprintf(stderr, "--- thread dump: %d threads\n", signalled);
    return signalled;
#else
    return 0;
#endif
}
