/* compat_darwin.c on Linux: the Darwin stand-ins for timed locks, clock_nanosleep and getrandom. */
#define _GNU_SOURCE
#include "compat_darwin.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static struct timespec in_ms(long ms) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME,&t);
    t.tv_nsec+=ms*1000000;
    t.tv_sec+=t.tv_nsec/1000000000; t.tv_nsec%=1000000000;
    return t;
}
static double ms_since(const struct timespec *a) {
    struct timespec b; clock_gettime(CLOCK_MONOTONIC,&b);
    return (b.tv_sec-a->tv_sec)*1e3+(b.tv_nsec-a->tv_nsec)/1e6;
}
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t rw=PTHREAD_RWLOCK_INITIALIZER;
static void *release_later(void *unused) {
    (void)unused;
    struct timespec t={0,30000000}; nanosleep(&t,NULL);
    pthread_mutex_unlock(&mutex);
    return NULL;
}
static void *hold_read(void *unused) {
    (void)unused;
    struct timespec deadline=in_ms(500);
    assert(compat_rwlock_timedrdlock(&rw,&deadline)==0); /* readers share */
    pthread_rwlock_unlock(&rw);
    return NULL;
}

int main(void) {
    struct timespec start, deadline;
    /* Free mutex: taken at once. Held one: ETIMEDOUT near the deadline. Released meanwhile: taken. */
    deadline=in_ms(100);
    assert(compat_mutex_timedlock(&mutex,&deadline)==0);
    pthread_t other;
    pthread_mutex_t held=PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&held);
    clock_gettime(CLOCK_MONOTONIC,&start);
    deadline=in_ms(40);
    assert(compat_mutex_timedlock(&held,&deadline)==ETIMEDOUT);
    double waited=ms_since(&start);
    assert(waited>=39 && waited<80);
    assert(pthread_create(&other,NULL,release_later,NULL)==0);
    deadline=in_ms(1000);
    clock_gettime(CLOCK_MONOTONIC,&start);
    assert(compat_mutex_timedlock(&mutex,&deadline)==0);
    waited=ms_since(&start);
    assert(waited>=25 && waited<60);
    pthread_join(other,NULL);
    struct timespec bad={0,1000000000};
    assert(compat_mutex_timedlock(&mutex,&bad)==EINVAL);
    pthread_mutex_unlock(&mutex);

    /* A writer excludes readers and writers until it unlocks. */
    deadline=in_ms(100);
    assert(compat_rwlock_timedwrlock(&rw,&deadline)==0);
    deadline=in_ms(20);
    assert(compat_rwlock_timedrdlock(&rw,&deadline)==ETIMEDOUT);
    deadline=in_ms(20);
    assert(compat_rwlock_timedwrlock(&rw,&deadline)==ETIMEDOUT);
    pthread_rwlock_unlock(&rw);
    assert(pthread_create(&other,NULL,hold_read,NULL)==0); pthread_join(other,NULL);

    /* Absolute and relative clock_nanosleep. */
    struct timespec target;
    clock_gettime(CLOCK_MONOTONIC,&start);
    target=start; target.tv_nsec+=20000000; target.tv_sec+=target.tv_nsec/1000000000; target.tv_nsec%=1000000000;
    assert(compat_clock_nanosleep(CLOCK_MONOTONIC,1,&target,NULL)==0);
    waited=ms_since(&start);
    assert(waited>=19.9 && waited<40);
    struct timespec past={0,0};
    assert(compat_clock_nanosleep(CLOCK_MONOTONIC,1,&past,NULL)==0);
    struct timespec rel={0,10000000};
    clock_gettime(CLOCK_MONOTONIC,&start);
    assert(compat_clock_nanosleep(CLOCK_MONOTONIC,0,&rel,NULL)==0 && ms_since(&start)>=9.9);

    /* getrandom beyond getentropy's 256-byte limit. */
    unsigned char a[1000]={0}, b[1000]={0};
    assert(compat_getrandom(a,sizeof(a),0)==sizeof(a) && compat_getrandom(b,sizeof(b),0)==sizeof(b));
    assert(memcmp(a,b,sizeof(a)));
    assert(compat_gettid()>0);
    puts("PASS: Darwin compat (timed locks, clock_nanosleep, getrandom, gettid)");
    return 0;
}
