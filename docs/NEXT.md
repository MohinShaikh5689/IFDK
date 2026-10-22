# Next tasks (handover, 2026-10-22 night)

## State on the device (0.1.18, full game CUSA01715 + 01.02 on the phone)
**The game boots and plays its intro**: brightness screen, DualSense input, menus, cutscene video with
FFmpeg. It crashes when **New Game** is chosen (below).

The black-screen hang of 0.1.15-0.1.17, and its fix (0.1.18, `native/cpu/guest_cpu_fex_darwin.cpp`):
- Darwin reports a write to a protected page as SIGBUS / `BUS_ADRALN`, the code of an alignment fault.
  `guest_cpu_handle_fault` sent it to FEXCore's `HandleUnalignedAccess`, whose "already backpatched by
  another thread" check returns 0 (run it again) for a plain STR. A zero store to a page that GPU page
  tracking protects (game offset `0x1a55de`) then faulted ~600,000 times a second and the tracker never saw
  it: black screen, phone hot. Now only ESR data fault status `0b100001` (alignment) goes to FEXCore.
- The first boot of the day got past it by luck: its faulting store was an STP, which FEXCore logs as
  `Unhandled JIT SIGBUS` and passes on to the tracker.
- 0.1.16 added the tools that found it: the loader dump samples the pc 16 times with the fault count around
  the samples, prints library+offset for `<redacted>` system symbols and maps JIT addresses to game offsets
  (`gow3_guest_rip_at`); probe.c counts every fault; a watchdog (`GOW3_FRAME_WATCHDOG=60`, graphics only)
  dumps the threads after 60 s without a flip and ends the app after 180 s; fault storms (>1000/s) are
  logged; thermal state changes go to the log; game files are excluded from backup.
- 0.1.17 (kept as a safety net, did not fire): 64 claimed GPU-tracking faults in a row on one page make
  the page writable (`probe.c`, logs `GPU tracking: 64 write faults in a row`).

## Next
1. **New Game crash** (log of 2026-10-22 17:04 UTC): FEXCore `[iOS-noexec]` at game offset `0x23f8600`
   (image + 0x23f8600 = 0x70024fc600), then `Guest fault (signal 11) ... address 0x0`. The image ends at
   0x1da483c (eboot + modules 0 and 1), so the target is outside any registered code range
   (`QueryGuestExecutableRange` = the image + the trap page): most likely a bad function pointer. 4 s
   before, iOS sent a critical memory warning (`TRIM_MEMORY_RUNNING_CRITICAL`) and the thermal state was
   `serious`; no failed allocation is logged. The stack-scan return addresses are stale. To find the caller:
   log the last guest block entries (or the indirect branch source) when NoExec fires, check what the
   runtime mapped at that address (flexible/direct memory map log), and measure `phys_footprint` at the
   time. Compare with the same step on gow3_android if possible.
2. Performance and heat (now measurable in real play): 30 fps cap, lower render resolution + MetalFX,
   PAUSE spin loops, thread QoS, thermal-state adaptation, `FEX E:` debug noise.
3. AltStore shows the JIT helper expiring a day before the app; the installed profiles (`ideviceprovision`)
   show both renewed together each time, so it is only AltStore's display.

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
- App: **Start game** (graphics: Metal view over the app, two-finger double tap hides it), **CPU only**,
  **Copy logs** (share sheet), **Dump threads**.

## State on the device (0.1.15, 2026-10-22, without the game data on the phone)
Everything up to the game's first data loads works on the iPhone 17: FEXCore runs the eboot, the runtime
loads it, MoltenVK device/swapchain (1206x2622), dense buffer arenas, presenter, overlay, eop/intro/sound
threads, first graphics pipelines (7 ms each), DualSense through SDL3 (`pad opened`), CoreAudio (3 ports), MP3
through FFmpeg. It then waits on a black screen because `EXEC/` is missing (`BOOT0_01.GNF`,
`DEPENDENCYGRAPH.IFF`, `R_FX.WAD` not found). No crash.

Fixed on the way (each from a device log):
- 0.1.10: no sparse binding on MoltenVK -> dense arenas (64 MiB, `GOW3_ARENA_MB`), soft robustness2.
- 0.1.11: fault report: guest registers, stack return addresses, last 32 host calls (`CallHost` ring).
- 0.1.12: `GOW3_DMEM_MB=7104` (default patch "Texture Corruption Fix" needs +2048 MiB, as run.py sets).
- 0.1.13: controllers and audio through SDL3 (`SDL_SetMainReady`, `SDL_JOYSTICK_HIDAPI=0`).
- 0.1.14: guest window 4+8+28 GiB; it took all of iOS's 64 GiB hole and thread creation failed (no VA left).
- 0.1.15: FFmpeg 7.1.2 static (H.264/HEVC/AAC/MP3, swscale, swresample) for runtime_videodec.c / runtime_mp3.c.

## Next
1. **Device run with the full game** (`EXEC/`, `ENGLISH/`, `*.plt` incl. the 01.02 update in `gow3/game/`):
   Start game on 0.1.15, Copy logs. Goal: intro logos, intro video, title screen.
2. Likely issues on that run: first real GPU work through MoltenVK (shader translation errors, formats the
   A19 lacks: A2B10G10R10 SNORM/SINT, RGB32 sampling, D16S8 -> fallback), arena merges ("Migrating arena"),
   memory (`phys_footprint` under ~6 GB), first-compile stutter, FFmpeg video decode on device.
   Empty texture slots are written as nil by MoltenVK (its 1.4.2 source); add placeholder images only if a
   log shows a problem. AvPlayer is still a stub (`ios/avplayer_stub.cpp`); enable it if the game imports it.
3. Later (UX): save backup/restore in the app (saves are `gow3/user/savedata/1/<CUSA>/`, same format as the PC
   port), touch controls, settings screen, boot.bin preparation on the phone (port scripts/prepare.py).
4. Later (performance): `FEX E:` debug noise from the FEX fork, JIT pool size, render resolution patches
   (480p/720p) + MetalFX upscaling, thermals.

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
