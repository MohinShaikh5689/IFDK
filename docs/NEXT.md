# Next tasks (handover, 2026-10-22)

## GPU step 2: shadPS4's video core in the app (this branch)
- `native/runtime/gpu/` is gow3_android `gpu/` (imported unmodified, then iOS commits marked `gow3 (iOS)`).
  `native/cmake/gow3gpu.cmake` builds it as `libgow3gpu.a` inside the FEX tree (FEX's fmt and xxhash);
  `gow3gpu_deps.cmake` holds the parts CMake forbids in deferred code (OBJCXX, sirit, miniz).
  New submodules: Vulkan-Headers v1.4.344 (as gow3_android; FEX's 1.4.337 is too old), VMA 3.3.0,
  robin-map 1.4.1; SDL3 3.2.24 is a pinned tarball (static, no GL/GLES), Boost and glslang come from Homebrew.
- Left out on iOS: FSR 3/4 (`ios/ffx_stub.c` fails their calls), AvPlayer (`ios/avplayer_stub.cpp`, needs
  FFmpeg), Zydis/xbyak (x86-64 SRT walker JIT; ARM64 interprets). `gow3_png` decodes with ImageIO.
- iOS changes: page tracking at 16 KiB (`TRACKER_PAGE_BITS` 14), `ios/window_ios.mm` (Frontend::WindowSDL on
  the app's CAMetalLayer via `gow3_gpu_set_surface`; text dialog confirms its initial text), the Vulkan
  dispatcher from MoltenVK's linked `vkGetInstanceProcAddr`, `ios/gow3_darwin_compat.h` (gettid), utility-QoS
  helper threads, user paths in `Library/Caches/gow3` (static init, before main), no nullDescriptor /
  robustBufferAccess2 (soft on Apple; null buffer bindings get a zeroed stand-in).
- Local check on Linux x86-64 (catches dependency/API problems before CI): configure FEX with
  `-DENABLE_X86_HOST_DEBUG=ON -DENABLE_FEX_ALLOCATOR=OFF -DCMAKE_PROJECT_FEX_INCLUDE=native/cmake/fex_project.cmake`
  and `ninja gow3gpu -k 0`; only xbyak, FFmpeg and the .mm fail there (Linux-only).
- CI: `.github/workflows/gpu.yml` (GOW3_BUILD=gpu-check, error summary). The IPA workflow builds and links it.
- App 0.1.9: **Start game** (graphics: Metal view over the app, two-finger double tap hides it) and **CPU only**.

## Next
0. **Sparse buffers: confirmed missing on the A19 (0.1.9 log), now handled (0.1.10).** Without a sparse queue,
   BufferCache uses dense arenas: 64 MiB device-local buffers (`GOW3_ARENA_MB`, 16-1024) made when the GPU
   first touches their range; EnsureResident only writes the BDA page table; a request crossing two arenas
   merges them with a GPU copy (old arenas stay allocated). Watch "Migrating arena" and GPU memory in the log;
   the alternative with no copies at all is importing guest memory (VK_EXT_external_memory_host is present).
1. Device test of 0.1.10: Start game; read `Documents/gow3/loader.log` (GPU init, instance/device creation,
   the robustness2 warning, first `sceVideoOutOpen`, first frame). Full game files must be on the phone.
2. Expected device issues: null *image* descriptors (vk_rasterizer.cpp binds VK_NULL_HANDLE image views for
   unbound textures; needs a dummy view per type if MoltenVK rejects them), sparse buffer arenas (MoltenVK has
   no sparse binding; check the BufferCache probe falls back), geometry shaders (warning only), pipeline
   compile times (SPIR-V -> MSL) and memory.
3. Controllers and audio (0.1.13): runtime_pad.c and runtime_audio.c are built against the app's SDL3
   (SDL_SetMainReady in gow3_start, SDL_JOYSTICK_HIDAPI=0 so CoreBluetooth is never opened; pads via
   GameController). The iOS window thread pumps SDL events into Gow3Overlay (R3+L2 menu). Untested on device.
   Empty texture slots: MoltenVK writes nil textures for null image views (checked in its 1.4.2 source), so no
   placeholder images yet. Direct memory: 7104 MiB (0.1.12), as run.py sets it for the default patch.
4. Touch controls, FFmpeg for iOS (AvPlayer, mp3, videodec), boot.bin preparation on the phone.

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
- **FFmpeg for iOS** for `runtime_mp3.c` and `runtime_videodec.c` (cutscenes): built by build-ios.sh since 0.1.15.
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
