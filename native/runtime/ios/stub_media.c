/* SPDX-License-Identifier: GPL-3.0-or-later
 * runtime_audio.c and runtime_pad.c (SDL3) and runtime_mp3.c / runtime_videodec.c (FFmpeg) until
 * their iOS backends exist (AudioQueue, GameController, FFmpeg for iOS). They resolve nothing, so a
 * --cpu-only run stops at the game's first audio, pad or video import, which names it. */
#include "runtime.h"
#include <stdio.h>

uintptr_t runtime_audio_resolve(const char *name) { (void)name; return 0; }
void runtime_audio_report(void) { puts("Runtime: audio not in this build"); }
uintptr_t runtime_pad_resolve(const char *name) { (void)name; return 0; }
void runtime_pad_report(void) { puts("Runtime: pad not in this build"); }
int runtime_pad_describe(void) { return 0; }
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
