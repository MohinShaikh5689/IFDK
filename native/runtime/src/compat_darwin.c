/* See compat_darwin.h. Plain POSIX apart from the thread id, so it also builds (and is tested) on Linux. */
#define _GNU_SOURCE
#include "compat_darwin.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif

static int64_t ns_until(clockid_t clock, const struct timespec *deadline) {
    struct timespec now;
    clock_gettime(clock,&now);
    return (int64_t)(deadline->tv_sec-now.tv_sec)*1000000000+(deadline->tv_nsec-now.tv_nsec);
}
/* Polls `attempt` until it stops reporting EBUSY or the CLOCK_REALTIME deadline passes. Starts at 50 us
 * between attempts and backs off to 1 ms, so short waits stay short and long ones stay cheap. */
static int poll_until(int (*attempt)(void *), void *object, const struct timespec *deadline) {
    if (!deadline || deadline->tv_nsec<0 || deadline->tv_nsec>=1000000000) return EINVAL;
    for (int64_t step=50000;;) {
        int e=attempt(object);
        if (e!=EBUSY) return e;
        int64_t left=ns_until(CLOCK_REALTIME,deadline);
        if (left<=0) return ETIMEDOUT;
        int64_t wait=left<step ? left : step;
        struct timespec t={(time_t)(wait/1000000000),(long)(wait%1000000000)};
        nanosleep(&t,NULL);
        if (step<1000000) step*=2;
    }
}
static int try_mutex(void *m) { return pthread_mutex_trylock(m); }
static int try_read(void *l) { return pthread_rwlock_tryrdlock(l); }
static int try_write(void *l) { return pthread_rwlock_trywrlock(l); }
int compat_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline) { return poll_until(try_mutex,mutex,deadline); }
int compat_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *deadline) { return poll_until(try_read,lock,deadline); }
int compat_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *deadline) { return poll_until(try_write,lock,deadline); }

int compat_clock_nanosleep(clockid_t clock, int flags, const struct timespec *request, struct timespec *remain) {
    if (!request || request->tv_nsec<0 || request->tv_nsec>=1000000000) return EINVAL;
    if (!(flags & 1)) return nanosleep(request,remain) ? errno : 0; /* relative */
    for (;;) {
        int64_t left=ns_until(clock,request);
        if (left<=0) return 0;
        struct timespec t={(time_t)(left/1000000000),(long)(left%1000000000)};
        if (nanosleep(&t,NULL) && errno==EINTR) return EINTR;
    }
}

ssize_t compat_getrandom(void *buffer, size_t size, unsigned flags) {
    (void)flags;
#ifdef __APPLE__
    arc4random_buf(buffer,size);
    return (ssize_t)size;
#else
    /* getentropy fills at most 256 bytes per call. */
    for (size_t done=0; done<size;) {
        size_t n=size-done<256 ? size-done : 256;
        if (getentropy((unsigned char *)buffer+done,n)) return -1;
        done+=n;
    }
    return (ssize_t)size;
#endif
}

uint64_t compat_gettid(void) {
#ifdef __APPLE__
    uint64_t id=0;
    pthread_threadid_np(NULL,&id);
    return id;
#else
    return (uint64_t)gettid();
#endif
}

#undef getrusage
int compat_getrusage(int who, struct rusage *usage) {
#ifdef __APPLE__
    if (who==1) {
        thread_basic_info_data_t info;
        mach_msg_type_number_t count=THREAD_BASIC_INFO_COUNT;
        mach_port_t thread=mach_thread_self();
        kern_return_t kr=thread_info(thread,THREAD_BASIC_INFO,(thread_info_t)&info,&count);
        mach_port_deallocate(mach_task_self(),thread);
        if (kr!=KERN_SUCCESS) { errno=EINVAL; return -1; }
        *usage=(struct rusage){0};
        usage->ru_utime=(struct timeval){info.user_time.seconds,info.user_time.microseconds};
        usage->ru_stime=(struct timeval){info.system_time.seconds,info.system_time.microseconds};
        return 0;
    }
#endif
    return getrusage(who,usage);
}
