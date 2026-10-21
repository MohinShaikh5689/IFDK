# Next tasks (handover, 2026-10-21 evening)

**Status:** all on `main`. App **0.1.8** (Actions → "iOS IPA" on main → artifact `gow3-ios-Release-ipa`).
- CPU side boots: module 1 (fixed: FEX unaligned-atomic backpatch now writes through the JIT pool's RW
  alias, `native/patches/fex/0001-*.patch`), the game's entry runs, 6 guest threads start, and the
  CPU-only run stops by design at `sceVideoOutOpen` (first GPU import).
- GPU step 1 done: `native/gpu/screen_test.c` + `Gow3GameView.swift`. MoltenVK 1.4.2 on the A19:
  Vulkan 1.3, 60 fps at 1206x2622, all four extensions shadPS4 requires are present.
  Missing: `nullDescriptor` and `robustBufferAccess2` (shadPS4 asserts on both: replace the two
  VK_NULL_HANDLE buffer bindings in vk_rasterizer.cpp with a dummy buffer, drop the robustness asserts),
  `geometryShader` (shadPS4 skips those draws with a warning; GoW3's renderer comes from the PS3, which
  had no GS, so likely unused; log the warning on device to confirm), `logicOp`, `shaderFloat64`.
- Two game files were not found in the CPU-only run (EXEC/...): the full game must be on the phone
  for the GPU work (`ENGLISH/`, `EXEC/` (30 GB), `sce_module/`, both `.plt` from the update).

## Next: GPU step 2, shadPS4's video core in the app
1. Build gow3_android `gpu/` for iOS (static, inside build-ios.sh): Boost headers, fmt, VMA, sirit,
   Zydis, magic_enum, robin-map, xxhash, miniz, imgui; host shaders with glslang on the runner.
   Leave out FSR 3/4, FFmpeg (avplayer: stub), SDL3.
2. Port `video_core/page_manager.cpp` (futex, MAP_NORESERVE, 4 KiB tracking → 16 KiB),
   `common/signal_context.cpp` / `shim/gow3gpu.cpp` fault context to Darwin `__ss`.
3. Window: replace `shim/window.cpp` (SDL3) with Gow3GameView's CAMetalLayer (VK_EXT_metal_surface).
4. Replace `native/runtime/ios/stub_gpu.c`, drop `--cpu-only`, boot to the first frame.

---
## Earlier handover (morning of 2026-10-21)

## Where things stand
| Area | State |
|---|---|
| Built-in JIT (no StikDebug) | **Works on the iPhone 17** (in-app pairing, helper extension). docs/JIT.md |
| Memory | AltStore keeps `increased-memory-limit`: 6143 MB limit, 64 GiB free window at `0x70_0000_0000`. File-backed guest memory costs 0 MB. Install/refresh **only with AltStore** (SideStore drops the entitlement). |
| GPU facts | A19: `bc_textures` true (PS4 BC1–BC7 textures sample directly), 4 GB max buffer, 5.3 GB working set |
| Runtime | `native/runtime` (gow3_android src/, iOS port). Compiles for iOS; Linux tests pass (`native/runtime/tests/run.sh`) |
| Loader | CPU-only start in the app works: boot file accepted, patches applied, image mapped, **libc (module 0) init ran through FEX and returned 0**, 2.8 GB direct memory mapped in the window |
| Blocker | The game thread **stops while starting linked module 1** (app stays responsive; no crash) |

## 1. Find why module 1 stops (do this first)
1. Copy the full game (`ENGLISH/`, `EXEC/`, `*.plt`) to `Documents/gow3/game/` to rule out missing files:
   ```sh
   ideviceinstaller -l | grep -i gow3            # bundle id (AltStore adds the team id)
   ifuse --documents <bundle id> ~/iphone
   cp -r game/ENGLISH game/EXEC game/sce_discmap.plt ~/iphone/gow3/game/
   cp "_Update to v1.02/game/"*.plt ~/iphone/gow3/game/
   fusermount -u ~/iphone
   ```
2. Install 0.1.5, Enable JIT, Start game (CPU only). When it stops at `Starting linked module 1`, wait ~20 s,
   tap **Dump threads**, and read `Documents/gow3/loader.log`: each thread prints guest RIP (`image-relative=`),
   its rbp chain, the thread name and the host symbols it is in.
3. Likely causes to check against the dump:
   - waiting on a runtime primitive (`runtime_sema.c`, `runtime_mutex.c`, cond waits): Darwin's CLOCK_REALTIME
     waits or the polled timed locks in `compat_darwin.c`;
   - a guest spin loop (phone gets warm): a TSC/time source (`guest_cpu_tsc`, `runtime_time.c`) or an atomic
     the guest polls;
   - a host call into a stub that never returns (`native/runtime/ios/stub_*.c`);
   - an unresolved import trap (would normally print `unresolved` and exit).
4. Useful extra logging: `GOW3_TRACE_SEMA=1` (set with `setenv` in `startGameCpuOnly` in
   `app/modules/gow3-core/ios/Gow3CoreModule.swift`) prints every blocking semaphore wait.
5. FEX prints `Failed to mprotect last page of code buffer`: its 4 KiB guard page cannot be protected on 16 KiB
   pages. Harmless so far; patch `PooledAllocatorVirtualWithGuard` to a host page later (docs/AUDIT.md).

## 2. Finish the runtime (milestone 4)
- **Audio**: replace `runtime_audio.c`'s SDL3 output with AudioQueue/AVAudioEngine (same `runtime_audio_resolve` exports).
- **Pad**: `runtime_pad.c` on the GameController framework (DualSense/DS4/Xbox/Backbone).
- **FFmpeg for iOS** for `runtime_mp3.c` and `runtime_videodec.c` (cutscenes). Until then `ios/stub_media.c`.
- **boot.bin on the phone**: port gow3's `scripts/prepare.py`, `link_libc.py`, `link_modules.py`,
  `content_profile.py`, `patches.py` (Python) to C or Swift, so no PC is needed.
- Add the new sources to `native/cmake/gow3core.cmake` and `native/runtime/build-ios-check.sh`.

## 3. GPU (milestone 5, the big one)
- Port gow3_android `gpu/` (shadPS4 video core + shim) to iOS: MoltenVK (static), a CAMetalLayer from a native
  fullscreen UIViewController (`window_ios.mm`), SPIR-V → MSL through MoltenVK.
- Smoke test first: `src/vulkan_smoke.c` on the device, then the title screen.
- Replace `native/runtime/ios/stub_gpu.c` with the real library; remove `--cpu-only` from the start button.
- Render low (gow3 already has 480p/720p patches) and upscale with MetalFX spatial; frame generation only later.

## 4. Optimisation (milestone 6)
- Measure `phys_footprint` while playing; keep anonymous memory under ~6 GB (guest pool is file-backed already).
- JIT pool size (256 MiB now; Madeira uses 640–896 MiB), FEX options, thermals.

## Reference
- docs/AUDIT.md: platform findings and device results. docs/JIT.md: built-in JIT. CLAUDE.md: layout and decisions.
- CI: `.github/workflows/ios.yml` (IPA, dispatch it on the branch), `.github/workflows/runtime.yml` (Linux tests + iOS compile check, runs on push).
