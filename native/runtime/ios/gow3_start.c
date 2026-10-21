/* SPDX-License-Identifier: GPL-3.0-or-later
 * The loader inside the iOS app: probe.c's main (gow3_probe_main on Apple) on a thread of its own,
 * with its output appended to a log file, since an app has no terminal and cannot start processes. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif

int gow3_probe_main(int argc, char **argv);

static int loader_argc;
static char **loader_argv;

static void *loader(void *unused) {
    (void)unused;
    int result = gow3_probe_main(loader_argc, loader_argv);
    /* The game normally never returns; the loader's own errors end the process through exit(). */
    printf("Loader returned %d\n", result);
    fflush(NULL);
    return NULL;
}

/* Starts the loader with argv[0..argc) (argv[0] is the program name, argv[1] the boot.bin path);
 * stdout and stderr go to log_path from here on. 0 when the thread started. Call once. */
int gow3_start(int argc, const char *const *argv, const char *log_path) {
    if (loader_argv || argc < 2) return -1;
    if (log_path && *log_path) {
        if (!freopen(log_path, "a", stdout) || !freopen(log_path, "a", stderr)) return -2;
        setvbuf(stderr, NULL, _IONBF, 0);
    }
    loader_argv = calloc((size_t)argc + 1, sizeof(char *));
    if (!loader_argv) return -3;
    for (int i = 0; i < argc; ++i) loader_argv[i] = strdup(argv[i]);
    loader_argc = argc;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20); /* host frames of guest calls nest deeply */
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    int e = pthread_create(&thread, &attr, loader, NULL);
    pthread_attr_destroy(&attr);
    return e ? -4 : 0;
}

/* Every other thread writes where it is (guest frames, host symbols) to the log: SIGUSR2 runs
 * probe.c's thread_dump. For a game that hangs. The number of threads signalled, or -1. */
int gow3_dump_threads(void) {
    if (!loader_argv) return -1;
    fprintf(stderr, "--- thread dump requested\n");
#ifdef __APPLE__
    thread_act_array_t threads;
    mach_msg_type_number_t count = 0;
    mach_port_t self = mach_thread_self();
    int signalled = 0;
    if (task_threads(mach_task_self(), &threads, &count) == KERN_SUCCESS) {
        for (mach_msg_type_number_t i = 0; i < count; ++i) {
            pthread_t thread = threads[i] != self ? pthread_from_mach_thread_np(threads[i]) : NULL;
            if (thread && !pthread_kill(thread, SIGUSR2)) { ++signalled; usleep(30000); }
            mach_port_deallocate(mach_task_self(), threads[i]);
        }
        vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(*threads));
    }
    mach_port_deallocate(mach_task_self(), self);
    fprintf(stderr, "--- thread dump: %d threads\n", signalled);
    return signalled;
#else
    return 0;
#endif
}
