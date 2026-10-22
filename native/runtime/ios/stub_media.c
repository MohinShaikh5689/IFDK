/* SPDX-License-Identifier: GPL-3.0-or-later
 * runtime_mp3.c / runtime_videodec.c (FFmpeg) until FFmpeg is built for iOS. They resolve nothing, so
 * the game stops at its first video decoder import, which names it. (Audio and pads: SDL3.) */
#include "runtime.h"
#include <stdio.h>

uintptr_t runtime_videodec_resolve(const char *name) { (void)name; return 0; }
int runtime_mp3_parse(const unsigned char *data, uint32_t size, int parse_ofl, int layer3_only, Mp3Frame *frame) {
    (void)data; (void)size; (void)parse_ofl; (void)layer3_only; (void)frame; return -1;
}
Mp3Decoder *runtime_mp3_open(void) { return NULL; }
void runtime_mp3_close(Mp3Decoder *decoder) { (void)decoder; }
void runtime_mp3_reset(Mp3Decoder *decoder) { (void)decoder; }
int runtime_mp3_decode(Mp3Decoder *decoder, const unsigned char *data, int size, float *pcm, int max_samples, int *channels) {
    (void)decoder; (void)data; (void)size; (void)pcm; (void)max_samples; (void)channels; return -1;
}
