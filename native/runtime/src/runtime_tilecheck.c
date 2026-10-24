/* SPDX-License-Identifier: GPL-2.0-or-later */
/* gow3: GOW3_TILE_CHECK=1 (log GOW3_TILE_LOG). On iOS, texture data lands in the wrong place in
 * guest memory (docs/NEXT.md), written by the game's own 1D tiler (tile_check.h). Every call of the
 * tiler is checked at each link of the chain, so the log names the one that breaks:
 *   in flight: a file read into the source has not returned yet (the game was woken too early);
 *   source:    the linear source against the game file it was read from, read again here;
 *   same file: the same file bytes tiled into a second destination (the duplicate data seen);
 *   tiling:    the tiled result against the source, element by element (micro-tile order as
 *              native/scripts/dumpcheck.py, which matched the GPU's view of these textures);
 *   later:     a watcher thread hashes every tiled range again and reports the first change. */
#include "runtime.h"
#include "guest_cpu.h"
#include "tile_check.h"
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int runtime_tilecheck_enabled;
static uintptr_t tiler_entry, image_base;
static FILE *out;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static double start_time;

enum { FDS = 1024, PATHS = 1024, READS = 1 << 15, FLIGHT = 64, TILES = 1 << 14, LOUD = 300 };
static char *paths[PATHS]; /* host path by file number % PATHS */
static uint32_t fd_file[FDS], files;
typedef struct { uintptr_t address; uint64_t size; int64_t offset; uint32_t file; } Read;
static Read reads[READS];
static uint64_t read_count;
static struct { uintptr_t address; uint64_t size; } flight[FLIGHT];
typedef struct {
    uintptr_t lo, hi, src;
    uint64_t hash, call;
    int64_t offset;
    uint32_t file;
    int changed;
    double time;
} Tile;
static Tile tiles[TILES];
static uint64_t calls, loud;
static struct {
    uint64_t checked, unchecked, bad_tiling, in_flight, bad_source, source_ok, no_source, src_changed,
        same_file, overlaps, retiled, changed;
} stats;

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9 - start_time;
}

static uint64_t hash(const unsigned char *p, uint64_t n) {
    uint64_t h = 0x9e3779b97f4a7c15u ^ n, w;
    for (; n >= 8; p += 8, n -= 8) {
        memcpy(&w, p, 8);
        h = (h ^ w) * 0x100000001b3u;
        h ^= h >> 29;
    }
    while (n--) h = (h ^ *p++) * 0x100000001b3u;
    return h;
}

/* hash() of guest memory that may be unmapped meanwhile: 0 when it is. */
static int safe_hash(uintptr_t address, uint64_t n, uint64_t *h) {
    sigjmp_buf recover;
    if (sigsetjmp(recover, 1)) return 0;
    runtime_fault_recover = &recover;
    *h = hash((const unsigned char *)address, n);
    runtime_fault_recover = NULL;
    return 1;
}

/* Problems also go to the loader log, the first LOUD of them. */
static void report(int problem, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void report(int problem, const char *format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    pthread_mutex_lock(&lock);
    if (out) fputs(line, out);
    pthread_mutex_unlock(&lock);
    if (problem && __atomic_fetch_add(&loud, 1, __ATOMIC_RELAXED) < LOUD) printf("Tile check: %s", line);
}

void runtime_tilecheck_open(int fd, const char *host_path) {
    if (!runtime_tilecheck_enabled || fd < 0 || fd >= FDS) return;
    pthread_mutex_lock(&lock);
    const uint32_t n = ++files;
    free(paths[n % PATHS]);
    paths[n % PATHS] = strdup(host_path);
    fd_file[fd] = n;
    pthread_mutex_unlock(&lock);
}

void runtime_tilecheck_read(int fd, const void *buffer, uint64_t size, int64_t offset) {
    if (!runtime_tilecheck_enabled || fd < 0 || fd >= FDS || offset < 0) return;
    pthread_mutex_lock(&lock);
    reads[read_count++ % READS] = (Read){(uintptr_t)buffer, size, offset, fd_file[fd]};
    pthread_mutex_unlock(&lock);
}

int runtime_tilecheck_read_begin(const void *buffer, uint64_t size) {
    if (!runtime_tilecheck_enabled) return -1;
    pthread_mutex_lock(&lock);
    int slot = -1;
    for (int i = 0; i < FLIGHT && slot < 0; ++i)
        if (!flight[i].size) slot = i;
    if (slot >= 0) {
        flight[slot].address = (uintptr_t)buffer;
        flight[slot].size = size;
    }
    pthread_mutex_unlock(&lock);
    return slot;
}

void runtime_tilecheck_read_end(int slot) {
    if (slot < 0) return;
    pthread_mutex_lock(&lock);
    flight[slot].size = 0;
    pthread_mutex_unlock(&lock);
}

/* Micro-tile element order (native/scripts/dumpcheck.py pixel_index): 0 display, 1 thin. */
static unsigned element_index(unsigned x, unsigned y, unsigned micro, unsigned bpp) {
    const unsigned x0 = x & 1, x1 = x >> 1 & 1, x2 = x >> 2 & 1, y0 = y & 1, y1 = y >> 1 & 1, y2 = y >> 2 & 1;
    unsigned p[6] = {x0, y0, x1, y1, x2, y2};
    if (micro == 0) {
        const unsigned d[5][6] = {{x0, x1, x2, y1, y0, y2}, {x0, x1, x2, y0, y1, y2}, {x0, x1, y0, x2, y1, y2},
                                  {x0, y0, x1, x2, y1, y2}, {y0, x0, x1, x2, y1, y2}};
        const unsigned row = bpp == 8 ? 0 : bpp == 16 ? 1 : bpp == 32 ? 2 : bpp == 64 ? 3 : 4;
        memcpy(p, d[row], sizeof(p));
    }
    return p[0] | p[1] << 1 | p[2] << 2 | p[3] << 3 | p[4] << 4 | p[5] << 5;
}

/* The newest read holding src: its file, the file offset of src and the bytes it covers. Also how
 * many other reads in the window wrote over src. */
static int find_read(uintptr_t src, uint64_t n, Read *found, uint64_t *covered, unsigned *others) {
    int hit = 0;
    *others = 0;
    pthread_mutex_lock(&lock);
    const uint64_t first = read_count > READS ? read_count - READS : 0;
    for (uint64_t i = read_count; i-- > first;) {
        const Read *r = &reads[i % READS];
        if (src < r->address || src - r->address >= r->size) {
            if (hit && r->address < src + n && src < r->address + r->size) ++*others;
            continue;
        }
        if (!hit) {
            *found = *r;
            found->offset += (int64_t)(src - r->address);
            *covered = r->address + r->size - src < n ? r->address + r->size - src : n;
            hit = 1;
        } else {
            ++*others;
        }
    }
    pthread_mutex_unlock(&lock);
    return hit;
}

/* src against the file: "ok", or where it differs. */
static void check_source(uintptr_t src, uint64_t n, char *verdict, size_t size, Read *read) {
    uint64_t covered;
    unsigned others;
    read->file = 0;
    if (!find_read(src, n, read, &covered, &others)) {
        __atomic_add_fetch(&stats.no_source, 1, __ATOMIC_RELAXED);
        snprintf(verdict, size, "no read");
        return;
    }
    char path[1024] = "";
    pthread_mutex_lock(&lock);
    if (paths[read->file % PATHS]) snprintf(path, sizeof(path), "%s", paths[read->file % PATHS]);
    pthread_mutex_unlock(&lock);
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    unsigned char *file = malloc(covered);
    const int fd = file ? open(path, O_RDONLY) : -1;
    const ssize_t got = fd >= 0 ? pread(fd, file, covered, read->offset) : -1;
    if (fd >= 0) close(fd);
    if (got != (ssize_t)covered) {
        snprintf(verdict, size, "%s @%#" PRIx64 " (cannot read again)", name, (uint64_t)read->offset);
        free(file);
        return;
    }
    const unsigned char *s = (const unsigned char *)src;
    uint64_t first = UINT64_MAX, differ = 0;
    for (uint64_t i = 0; i < covered; ++i) {
        if (s[i] != file[i]) {
            if (first == UINT64_MAX) first = i;
            ++differ;
        }
    }
    free(file);
    if (differ) {
        __atomic_add_fetch(&stats.bad_source, 1, __ATOMIC_RELAXED);
        snprintf(verdict, size, "SOURCE DIFFERS from %s @%#" PRIx64 ": %" PRIu64 " of %" PRIu64
                 " bytes, first at +%#" PRIx64 " (%u other reads into it)", name, (uint64_t)read->offset,
                 differ, covered, first, others);
    } else {
        __atomic_add_fetch(&stats.source_ok, 1, __ATOMIC_RELAXED);
        snprintf(verdict, size, "%s @%#" PRIx64 " ok (%" PRIu64 "/%" PRIu64 " B; %u other reads into it)", name,
                 (uint64_t)read->offset, covered, n, others);
    }
}

static int in_flight(uintptr_t lo, uintptr_t hi) {
    int hit = 0;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < FLIGHT; ++i)
        if (flight[i].size && flight[i].address < hi && lo < flight[i].address + flight[i].size) hit = 1;
    pthread_mutex_unlock(&lock);
    return hit;
}

ABI uint64_t runtime_tilecheck_tile1d(uint64_t tiler, uint64_t dst, uint64_t src, uint64_t region, uint64_t pitch,
                                  uint64_t slice_pitch, uint64_t rbp) {
    const uint64_t args[6] = {tiler, dst, src, region, pitch, slice_pitch};
    if (!tiler || !dst || !src || !region) return guest_cpu_call(tiler_entry, 6, args);
    uint32_t t[20], r[6];
    memcpy(t, (const void *)(uintptr_t)tiler, sizeof(t));
    memcpy(r, (const void *)(uintptr_t)region, sizeof(r));
    uint64_t slice;
    memcpy(&slice, t + 18, 8);
    /* Tiler1d (constructor 0x241a20): +4 array mode, +8 micro-tile mode, +0xc width, +0x10 height,
     * +0x14 depth (elements), +0x24 bits per element, +0x30 thickness, +0x34 bytes per micro
     * tile, +0x38 micro tiles per row, +0x48 bytes per slice. Region: left top front right bottom back. */
    const unsigned micro = t[2], width = t[3], height = t[4], bpp = t[9], thickness = t[12],
                   tile_bytes = t[13], per_row = t[14];
    const unsigned w = r[3] - r[0], h = r[4] - r[1], d = r[5] - r[2], bytes = bpp / 8;
    const int sane = w && h && d && r[3] <= width && r[4] <= height && bytes && bpp % 8 == 0 && bytes <= 16 &&
                     per_row && tile_bytes == 64 * bytes * thickness && pitch >= w;
    if (!sane) {
        __atomic_add_fetch(&stats.unchecked, 1, __ATOMIC_RELAXED);
        const uint64_t result = guest_cpu_call(tiler_entry, 6, args);
        report(0, "%.3f unchecked dst %#" PRIx64 " src %#" PRIx64 " w %u h %u bpp %u region %u %u %u %u %u %u -> %#x\n",
               now(), dst, src, width, height, bpp, r[0], r[1], r[2], r[3], r[4], r[5], (unsigned)result);
        return result;
    }
    const uint64_t call = __atomic_add_fetch(&calls, 1, __ATOMIC_RELAXED);
    const double time = now();
    const uint64_t src_span = ((uint64_t)(d - 1) * slice_pitch + (uint64_t)(h - 1) * pitch + w) * bytes;
    const uint64_t row_bytes = (uint64_t)per_row * tile_bytes;
    const uintptr_t lo = dst + r[2] * slice + (r[1] / 8) * row_bytes;
    const uintptr_t hi = dst + (uint64_t)(r[5] - 1) * slice + ((r[4] + 7) / 8) * row_bytes;
    uintptr_t caller = 0, caller2 = 0;
    if (rbp) {
        uint64_t frame[2];
        memcpy(frame, (const void *)(uintptr_t)rbp, 16);
        caller = frame[1];
        if (frame[0]) memcpy(&caller2, (const void *)(uintptr_t)(frame[0] + 8), 8);
    }

    const int flying = in_flight(src, src + src_span);
    if (flying) __atomic_add_fetch(&stats.in_flight, 1, __ATOMIC_RELAXED);
    char source[512];
    Read read;
    check_source(src, src_span, source, sizeof(source), &read);
    const uint64_t src_hash = hash((const unsigned char *)(uintptr_t)src, src_span);

    const uint64_t result = guest_cpu_call(tiler_entry, 6, args);

    const int src_changed = hash((const unsigned char *)(uintptr_t)src, src_span) != src_hash;
    if (src_changed) __atomic_add_fetch(&stats.src_changed, 1, __ATOMIC_RELAXED);
    uint64_t bad = 0, checked = 0;
    unsigned bx = 0, by = 0, bz = 0;
    if ((micro == 0 || micro == 1) && thickness == 1 && !(uint32_t)result) {
        const unsigned char *s = (const unsigned char *)(uintptr_t)src, *t8 = (const unsigned char *)(uintptr_t)dst;
        for (unsigned z = r[2]; z < r[5]; ++z)
            for (unsigned y = r[1]; y < r[4]; ++y)
                for (unsigned x = r[0]; x < r[3]; ++x) {
                    const uint64_t at = z * slice + ((uint64_t)(y / 8) * per_row + x / 8) * tile_bytes +
                                        element_index(x & 7, y & 7, micro, bpp) * bytes;
                    const uint64_t from = ((uint64_t)(z - r[2]) * slice_pitch + (uint64_t)(y - r[1]) * pitch + (x - r[0])) * bytes;
                    ++checked;
                    if (memcmp(t8 + at, s + from, bytes)) {
                        if (!bad++) { bx = x; by = y; bz = z; }
                    }
                }
        __atomic_add_fetch(&stats.checked, 1, __ATOMIC_RELAXED);
        if (bad) __atomic_add_fetch(&stats.bad_tiling, 1, __ATOMIC_RELAXED);
    } else {
        __atomic_add_fetch(&stats.unchecked, 1, __ATOMIC_RELAXED);
    }
    const uint64_t dst_hash = hash((const unsigned char *)lo, hi - lo);

    /* Earlier tilings: overlapping ones (other than the same range tiled again) and ones from the
     * same file bytes into another place. */
    char also[256] = "";
    size_t used = 0;
    unsigned overlaps = 0, retiled = 0, same = 0;
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < TILES; ++i) {
        const Tile *e = &tiles[i];
        if (!e->call) continue;
        if (e->lo < hi && lo < e->hi) {
            if (e->lo == lo && e->hi == hi) {
                ++retiled;
            } else if (++overlaps <= 2 && used < sizeof(also) - 80) {
                used += (size_t)snprintf(also + used, sizeof(also) - used, " OVERLAPS #%" PRIu64 " %#" PRIxPTR "..%#" PRIxPTR " (%.3f)",
                                         e->call, e->lo, e->hi, e->time);
            }
        }
        if (read.file && e->file == read.file && e->offset == read.offset && e->lo != lo && ++same <= 2 &&
            used < sizeof(also) - 80) {
            used += (size_t)snprintf(also + used, sizeof(also) - used, " SAME FILE BYTES as #%" PRIu64 " %#" PRIxPTR " (%.3f)",
                                     e->call, e->lo, e->time);
        }
    }
    tiles[call % TILES] = (Tile){lo, hi, src, dst_hash, call, read.file ? read.offset : -1, read.file, 0, time};
    pthread_mutex_unlock(&lock);
    if (overlaps) __atomic_add_fetch(&stats.overlaps, 1, __ATOMIC_RELAXED);
    if (retiled) __atomic_add_fetch(&stats.retiled, 1, __ATOMIC_RELAXED);
    if (same) __atomic_add_fetch(&stats.same_file, 1, __ATOMIC_RELAXED);
    if (runtime_wtrace_enabled) runtime_wtrace(WTRACE_TILE, lo, hi - lo, src, call);

    char tiling[160];
    if (!checked)
        snprintf(tiling, sizeof(tiling), "tiling not checked (micro %u thickness %u)", micro, thickness);
    else if (bad)
        snprintf(tiling, sizeof(tiling), "TILING WRONG: %" PRIu64 " of %" PRIu64 " elements, first (%u,%u,%u)", bad,
                 checked, bx, by, bz);
    else
        snprintf(tiling, sizeof(tiling), "tiling ok (%" PRIu64 " elements)", checked);
    const int problem = flying || bad || src_changed || same || strstr(source, "DIFFERS") != NULL;
    report(problem,
           "#%" PRIu64 " %.3f dst %#" PRIx64 " [%#" PRIxPTR "..%#" PRIxPTR "] src %#" PRIx64 "+%#" PRIx64
           " %ux%u bpp %u micro %u region %u,%u,%u..%u,%u,%u pitch %" PRIu64 " slice %" PRIu64
           " caller %#" PRIxPTR " %#" PRIxPTR " -> %#x | %s | %s%s%s%s retiled %u\n",
           call, time, dst, lo, hi, src, src_span, width, height, bpp, micro, r[0], r[1], r[2], r[3], r[4], r[5],
           pitch, slice_pitch, caller ? caller - image_base : 0, caller2 ? caller2 - image_base : 0, (unsigned)result,
           source, tiling, flying ? " | READ IN FLIGHT into the source" : "",
           src_changed ? " | SOURCE CHANGED during tiling" : "", also, retiled);
    return result;
}

/* Hashes every tiled range again; the first change of each is reported. */
static void *watcher(void *unused) {
    (void)unused;
    for (unsigned round = 1;; ++round) {
        struct timespec pause = {3, 0};
        nanosleep(&pause, NULL);
        for (unsigned i = 0; i < TILES; ++i) {
            pthread_mutex_lock(&lock);
            const Tile e = tiles[i];
            pthread_mutex_unlock(&lock);
            if (!e.call || e.changed) continue;
            uint64_t h;
            const int mapped = safe_hash(e.lo, e.hi - e.lo, &h);
            if (mapped && h == e.hash) continue;
            pthread_mutex_lock(&lock);
            const int same = tiles[i].call == e.call;
            if (same) tiles[i].changed = 1;
            pthread_mutex_unlock(&lock);
            if (!same) continue;
            __atomic_add_fetch(&stats.changed, 1, __ATOMIC_RELAXED);
            report(0, "#%" PRIu64 " %.3f changed after tiling at %.3f: [%#" PRIxPTR "..%#" PRIxPTR "]%s\n", e.call, now(),
                   e.time, e.lo, e.hi, mapped ? "" : " (unmapped)");
        }
        if (round % 4 == 0) {
            printf("Tile check: %" PRIu64 " tilings: %" PRIu64 " checked, %" PRIu64 " wrong, %" PRIu64 " not checked; source: %"
                   PRIu64 " ok, %" PRIu64 " DIFFERS, %" PRIu64 " no read, %" PRIu64 " read in flight, %" PRIu64
                   " changed during tiling; %" PRIu64 " same file bytes twice, %" PRIu64 " overlaps, %" PRIu64
                   " retiled, %" PRIu64 " changed later\n",
                   __atomic_load_n(&calls, __ATOMIC_RELAXED), stats.checked, stats.bad_tiling, stats.unchecked,
                   stats.source_ok, stats.bad_source, stats.no_source, stats.in_flight, stats.src_changed,
                   stats.same_file, stats.overlaps, stats.retiled, stats.changed);
            pthread_mutex_lock(&lock);
            if (out) fflush(out);
            pthread_mutex_unlock(&lock);
        }
    }
    return NULL;
}

void runtime_tilecheck_start(uintptr_t tiler, uintptr_t image) {
    tiler_entry = tiler;
    image_base = image;
    start_time = 0;
    start_time = now();
    const char *path = getenv("GOW3_TILE_LOG");
    out = fopen(path && *path ? path : "tiles.txt", "w");
    printf("Tile check: 1D tiler at %#" PRIxPTR " checked, log %s\n", tiler, out ? (path && *path ? path : "tiles.txt") : "(cannot write)");
    pthread_t thread;
    pthread_create(&thread, NULL, watcher, NULL);
    pthread_detach(thread);
}
