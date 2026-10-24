/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GOW3_TILE_CHECK_H
#define GOW3_TILE_CHECK_H
#include <stdint.h>
#include "runtime.h"
#include "signature.h"

/* CUSA01623 v01.02: the game's statically linked GPU address library. tileSurfaceRegion (image
 * offset 0x243a20) picks a tiler by array mode; its 1D path (every BC texture of the game: tile
 * mode 13, array mode 2) calls Tiler1d::tileSurfaceRegion at 0x241f10 from one place, 0x243bfb:
 * rdi the Tiler1d, rsi the tiled destination, rdx the linear source, rcx (= r12) the region,
 * r8 the source pitch and r9 the source slice pitch, in elements. rbp is the caller's frame. */
static const Gow3Signature gow3_tile1d_call_site = {
    "4c 89 f7 48 89 de 4c 89 ea 4c 89 e1 44 8b 04 24 44 8b 4c 24 04 e8 ?? ?? ?? ?? eb 9d", 21};
static const Gow3Signature gow3_tile1d_entry = {
    "55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81 ec 98 00 00 00 45 89 c7 49 89 d4 48 89 f3 49 89 fd 48 85 db", 0};

/* Stub for the rewritten call: the caller's rbp as the seventh argument (on the stack), then the
 * host check, which calls the original tiler. */
static inline unsigned gow3_tile1d_build_stub(unsigned char out[20], uintptr_t helper) {
    static const unsigned char code[20] = {
        0x55,                          /* push rbp (seventh argument) */
        0x48,0xb8, 0,0,0,0,0,0,0,0,    /* movabs rax, helper */
        0xff,0xd0,                     /* call rax */
        0x48,0x8d,0x64,0x24,0x08,      /* lea rsp, [rsp+8] */
        0xc3,                          /* ret */
        0xcc
    };
    memcpy(out, code, sizeof(code));
    memcpy(out + 3, &helper, 8);
    return sizeof(code);
}

/* runtime_tilecheck.c (GOW3_TILE_CHECK=1): checks every 1D tiling against its source and the
 * game file it was read from, and watches the result for later writes. */
void runtime_tilecheck_start(uintptr_t tiler, uintptr_t image);
ABI uint64_t runtime_tilecheck_tile1d(uint64_t tiler, uint64_t dst, uint64_t src, uint64_t region,
                                      uint64_t pitch, uint64_t slice_pitch, uint64_t rbp);
#endif
