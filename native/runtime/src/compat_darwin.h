/* Darwin (iOS) counterparts of the Linux calls the runtime uses, like compat_win.h for Windows.
 * Included through runtime.h before the sources' own system headers; it includes the headers whose
 * functions it replaces first, so the macros below only change calls in this project's sources.
 * compat_darwin.c implements them in portable POSIX, so tests/test_compat_darwin.c runs on Linux. */
#ifndef GOW3_COMPAT_DARWIN_H
#define GOW3_COMPAT_DARWIN_H
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>

/* Timed locks with CLOCK_REALTIME deadlines (Darwin has neither): trylock polled until the deadline. */
int compat_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline);
int compat_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *deadline);
int compat_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *deadline);
/* clock_nanosleep (Darwin has none): relative or TIMER_ABSTIME sleeps on any clock_gettime clock. */
int compat_clock_nanosleep(clockid_t clock, int flags, const struct timespec *request, struct timespec *remain);
/* getrandom (Darwin has getentropy and arc4random). */
ssize_t compat_getrandom(void *buffer, size_t size, unsigned flags);
/* The calling thread's system-wide id (gettid on Linux). */
uint64_t compat_gettid(void);

#ifdef __APPLE__
#include <sys/random.h>
#define pthread_mutex_timedlock compat_mutex_timedlock
#define pthread_rwlock_timedrdlock compat_rwlock_timedrdlock
#define pthread_rwlock_timedwrlock compat_rwlock_timedwrlock
#define clock_nanosleep compat_clock_nanosleep
#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif
#define getrandom compat_getrandom
#define gettid compat_gettid
#ifndef CLOCK_REALTIME_COARSE
#define CLOCK_REALTIME_COARSE CLOCK_REALTIME
#endif
/* Darwin names only the calling thread, which is the only one the runtime names. */
static inline int compat_setname(pthread_t thread, const char *name) { (void)thread; return pthread_setname_np(name); }
#define pthread_setname_np(thread, name) compat_setname(thread, name)
#endif
#endif
