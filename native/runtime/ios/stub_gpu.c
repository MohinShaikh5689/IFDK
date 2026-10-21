/* SPDX-License-Identifier: GPL-3.0-or-later
 * The GPU library (gpu/gow3gpu.h, shadPS4's video core) until it is ported (milestone 5): enough for
 * the loader's --cpu-only runs. Nothing is registered, so the game's GPU imports stay unresolved. */
#include "gpu/gow3gpu.h"
#include "runtime.h"
#include <stdio.h>

void gow3gpu_register_kernel(void) {}
int gow3gpu_init(const Gow3GpuConfig *config) {
    (void)config;
    fputs("GPU: not available in this build (run with --cpu-only)\n", stderr);
    return -1;
}
uintptr_t gow3gpu_resolve(const char *scoped_nid) { (void)scoped_nid; return 0; }
int gow3gpu_handle_fault(void *ucontext, void *address) { (void)ucontext; (void)address; return 0; }
void gow3gpu_dump_guest_writes(void *ucontext) { (void)ucontext; }
int gow3gpu_text_input_begin(const char *initial_utf8, const char *prompt_utf8) { (void)initial_utf8; (void)prompt_utf8; return -1; }
int gow3gpu_text_input_poll(char *out_utf8, uint64_t size) { (void)out_utf8; (void)size; return -1; }
int gow3gpu_choice_begin(const char *title, const char *const *items, const char *const *images, int count, int focus) {
    (void)title; (void)items; (void)images; (void)count; (void)focus; return -1;
}
int gow3gpu_choice_poll(void) { return -1; }
int gow3gpu_overlay_captures_input(void) { return 0; }
int gow3gpu_vibration_percent(void) { return 100; }
void gow3gpu_movie_picture(void) {}
unsigned gow3gpu_symbol_count(void) { return 0; }
int vulkan_smoke(void) { fputs("vulkan_smoke: not in this build\n", stderr); return 1; }

/* The in-game overlay's cheats and stat hooks (gpu/shim), installed only with the GPU (probe.c). */
ABI float gow3_orbs_gain(void *player, float requested) { (void)player; return requested; }
void gow3_orbs_status(int supported) { (void)supported; }
uintptr_t gow3_cheat_flag(unsigned id) { (void)id; return 0; }
uintptr_t gow3_actor_slots_address(void) { return 0; }
void gow3_actor_status(int supported) { (void)supported; }
void gow3_cheat_status(unsigned id) { (void)id; }
uintptr_t gow3_damage_multiplier(int taken) { (void)taken; return 0; }
uintptr_t gow3_orb_pickup_multiplier(unsigned kind) { (void)kind; return 0; }
void gow3_stat_multipliers_status(int damage, int orb_pickup) { (void)damage; (void)orb_pickup; }
