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

/* Flips the game submitted (VideoOutDriver::SubmitFlip); the watchdog below watches it. */
unsigned long long gow3_flip_count;
#ifdef __APPLE__
/* probe.c: faults that reached its handler, the last one, and where the game image is. */
extern unsigned long long gow3_fault_count;
extern int gow3_fault_last_sig, gow3_fault_last_code;
extern uintptr_t gow3_fault_last_addr, gow3_fault_last_pc;
uintptr_t gow3_guest_image_base(void);
/* guest_cpu_fex_darwin.cpp: the guest RIP of a host pc in JIT code, or 0. */
uint64_t gow3_guest_rip_at(uintptr_t pc);
#endif
int gow3_dump_threads(void);

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

#ifdef __APPLE__
static void log_faults(const char *prefix) {
    fprintf(stderr, "%sfaults=%llu last: signal %d code %d address %p host pc %p\n", prefix,
            __atomic_load_n(&gow3_fault_count, __ATOMIC_RELAXED), gow3_fault_last_sig, gow3_fault_last_code,
            (void *)gow3_fault_last_addr, (void *)gow3_fault_last_pc);
}
#endif

/* A game that shows no new frame for `limit` seconds is stuck: its threads are dumped to the log, once
 * per stall. After 3 * limit seconds without one the app ends, so that a hung game does not keep the
 * CPU busy (and the phone hot) until someone notices. */
static int watchdog_limit;

static void *frame_watchdog(void *unused) {
    (void)unused;
#ifdef __APPLE__
    pthread_setname_np("gow3:watchdog");
#endif
    enum { TICK = 5 };
    unsigned long long last_flips = 0;
#ifdef __APPLE__
    unsigned long long last_faults = 0;
#endif
    int stalled = 0, dumped = 0;
    for (;;) {
        sleep(TICK);
        stalled += TICK;
        const unsigned long long flips = __atomic_load_n(&gow3_flip_count, __ATOMIC_RELAXED);
        if (flips != last_flips) { last_flips = flips; stalled = 0; dumped = 0; }
#ifdef __APPLE__
        const unsigned long long faults = __atomic_load_n(&gow3_fault_count, __ATOMIC_RELAXED);
        if (faults - last_faults > 1000u * TICK) {
            fprintf(stderr, "watchdog: %llu faults in %d s; ", faults - last_faults, TICK);
            log_faults("");
        }
        last_faults = faults;
#endif
        if (stalled >= watchdog_limit && !dumped) {
            fprintf(stderr, "watchdog: no new frame for %d s (%llu flips so far); thread dump follows\n", stalled, flips);
            gow3_dump_threads();
            dumped = 1;
        }
        if (stalled >= 3 * watchdog_limit) {
            fprintf(stderr, "watchdog: no new frame for %d s; ending the app so a hung game does not keep the phone busy\n", stalled);
            fflush(NULL);
            _exit(3);
        }
    }
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
    /* GOW3_FRAME_WATCHDOG: seconds without a new frame before the watchdog acts (0 or unset: off). */
    const char *limit = getenv("GOW3_FRAME_WATCHDOG");
    watchdog_limit = limit ? atoi(limit) : 0;
    if (!e && watchdog_limit > 0) {
        pthread_attr_setstacksize(&attr, 256u << 10);
        pthread_create(&thread, &attr, frame_watchdog, NULL);
    }
    pthread_attr_destroy(&attr);
    return e ? -4 : 0;
}

#ifdef __APPLE__
/* Symbol, library and offset in it (system library symbols are <redacted> on iOS, the offset is not),
 * or the game instruction when the address is in JIT code. */
static void log_address(const char *label, uintptr_t address) {
    Dl_info where;
    if (dladdr((void *)address, &where) && where.dli_fname) {
        const char *library = strrchr(where.dli_fname, '/');
        fprintf(stderr, "    %s %s+0x%lx (%s+0x%lx)\n", label, where.dli_sname ? where.dli_sname : "?",
                (unsigned long)(address - (uintptr_t)where.dli_saddr), library ? library + 1 : where.dli_fname,
                (unsigned long)(address - (uintptr_t)where.dli_fbase));
        return;
    }
    const uint64_t rip = gow3_guest_rip_at(address);
    const uintptr_t image = gow3_guest_image_base();
    if (rip && image && rip - image < 0x10000000)
        fprintf(stderr, "    %s %p (JIT code for game offset 0x%lx)\n", label, (void *)address, (unsigned long)(rip - image));
    else if (rip)
        fprintf(stderr, "    %s %p (JIT code for guest rip 0x%llx)\n", label, (void *)address, (unsigned long long)rip);
    else
        fprintf(stderr, "    %s %p (no symbol: JIT code or stripped)\n", label, (void *)address);
}

/* The loader thread's pc, lr and frame-pointer chain, read through Mach while it is suspended,
 * so it shows up even when it cannot run a signal handler. Symbols are looked up after it resumes,
 * since it may hold the dyld or stdio lock. The pc is sampled a few more times, with the fault count
 * around the samples: a pc that never moves while faults climb is a fault retried forever. */
static void dump_loader_registers(thread_act_t thread) {
#if defined(__arm64__) || defined(__aarch64__)
    enum { DEPTH = 32, SAMPLES = 16 };
    uintptr_t frames[DEPTH + 2], samples[SAMPLES];
    int depth = 0, sampled = 0;
    const unsigned long long faults_before = __atomic_load_n(&gow3_fault_count, __ATOMIC_RELAXED);
    for (int i = 0; i < SAMPLES; ++i) {
        usleep(10000);
        arm_thread_state64_t sample;
        mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
        if (thread_suspend(thread) != KERN_SUCCESS) break;
        if (thread_get_state(thread, ARM_THREAD_STATE64, (thread_state_t)&sample, &count) == KERN_SUCCESS)
            samples[sampled++] = (uintptr_t)arm_thread_state64_get_pc(sample);
        thread_resume(thread);
    }
    const unsigned long long faults_after = __atomic_load_n(&gow3_fault_count, __ATOMIC_RELAXED);
    if (thread_suspend(thread) != KERN_SUCCESS) { fprintf(stderr, "    (cannot suspend the loader thread)\n"); return; }
    arm_thread_state64_t state;
    mach_msg_type_number_t n = ARM_THREAD_STATE64_COUNT;
    int have_state = thread_get_state(thread, ARM_THREAD_STATE64, (thread_state_t)&state, &n) == KERN_SUCCESS;
    if (have_state) {
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
    fprintf(stderr, "    x0 %016llx x1 %016llx x2 %016llx x3 %016llx sp %016llx cpsr %08x\n",
            (unsigned long long)state.__x[0], (unsigned long long)state.__x[1], (unsigned long long)state.__x[2],
            (unsigned long long)state.__x[3], (unsigned long long)arm_thread_state64_get_sp(state), state.__cpsr);
    fprintf(stderr, "    %d pc samples over ~%d ms, %llu faults meanwhile:\n", sampled, SAMPLES * 10, faults_after - faults_before);
    for (int i = 0; i < sampled; ++i) {
        int seen = 0;
        for (int j = 0; j < i; ++j) seen |= samples[j] == samples[i];
        if (seen) continue;
        int times = 0;
        for (int j = i; j < sampled; ++j) times += samples[j] == samples[i];
        char label[16];
        snprintf(label, sizeof(label), "x%d", times);
        log_address(label, samples[i]);
    }
    log_faults("    ");
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
