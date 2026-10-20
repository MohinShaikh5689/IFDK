# Porting audit: gow3_android → iOS

Sources: `ref/gow3_android` (wyatt-poole @ 5c2219d), `ref/fex-ios` (willfaust/FEX ios-port-2607 @ f59cab9),
`ref/MaxPS4` (@ 7297a3a), and Madeira files fetched from GitHub (docs/JIT.md, docs/BUILDING.md,
app/Madeira/JITAllocator.h, Madeira.entitlements, madeira-jit.js).

## Summary
- **16 KB pages are not a problem for the game runtime.** `src/runtime_memory.c` already uses `#define PAGE 16384`, since PS4 pages are 16 KB too.
  The "4 KB kernel" requirement in README.android.md comes from the Android FEX/rootfs stack, and Madeira's FEX fork already handles 16 KB pages on iOS.
- **FEXCore already builds natively for iOS.** fex-ios accepts `CMAKE_SYSTEM_NAME=iOS`. Madeira builds `FEX/build-ios` → `libFEXCore.a`, `libFEXCore_Base.a`, `libJemallocLibs.a`
  and External archives (cephes, fmt, SoftFloat-3e, xxhash) through `build/fex-ios/build.sh`.
  It also includes a dual-mapped JIT (`FEXCore::DualMap::WriteOffset`, set by the host before InitCore), iOS code-buffer limits, Apple allocator settings, and `sys_icache_invalidate`.
  The CPU layer needs the least new design work.
- **JIT has a working reference.** Madeira's `JITAllocator.c` plus `madeira-jit.js` (a StikDebug/StikJIT script using a BRK #0xf00d protocol) maps one big RX pool while the debugger is attached.
  Madeira's pool is 896 MB, marked no-footprint. The debugger then detaches. In-app pairing on iOS 27 uses StikJIT.xcframework (MPL-2.0) plus an app extension helper.
  The simplest first version just uses the StikDebug app (`stikdebug://enable-jit?...` with our bundle id, pid and script).
- **Biggest new risks:**
  1. The address-space layout: the game window sits above the standard iOS VA limit.
  2. Memory: a 5 GB+ guest pool on an 8 GB phone.
  3. MoltenVK coverage of what shadPS4 and the GoW3 fixes need.

## A. CPU boundary: `cpu/guest_cpu_fex.cpp`, `cpu/host_call_arm64.S`
| Item | Linux code | iOS work |
|---|---|---|
| Signal context | `uc_mcontext.regs[]`, `.pc`, `.sp`, FPSIMD record found in `__reserved` (`Fpsimd()`) | Darwin: `uc->uc_mcontext->__ss.__x[0..28]`, `__fp`, `__lr`, `__sp`, `__pc`, `__ns.__v[32]`. Rewrite `guest_cpu_signal_regs`, `guest_cpu_host_pc`, `guest_cpu_handle_fault` (JIT guard-page longjmp and call-return-stack reset write x25/pc). |
| Fault signal number | expects `SIGSEGV` for guard pages and protection faults | Darwin reports protection faults (write to a read-only page) as **SIGBUS**, and unmapped addresses as SIGSEGV. Accept both everywhere: here, `probe.c` `fault()`, and `gow3gpu_handle_fault`. |
| Unaligned atomics | `SIGBUS` + `BUS_ADRALN` → `HandleUnalignedAccess` | Darwin maps EXC_ARM_DA_ALIGN to SIGBUS/BUS_ADRALN; verify on device. A19 has LSE2, so these faults should be rare. |
| `process_vm_readv` (ReadGuest) | Linux only | `mach_vm_read_overwrite(mach_task_self(), ...)` |
| `mmap(... MAP_NORESERVE)` | call-return stack | Drop `MAP_NORESERVE`; reuse the fex-ios callret sizing (8 MiB, PR #19). |
| `guest_cpu_hook` | `mprotect` RWX on guest code, writes `syscall`, back to RX | Guest x86 code is never executed natively, only read by FEX, so map it **RW, never X**. Remove the EXEC bits. |
| `Setup48BitAllocatorIfExists` | reads Linux /proc maps | No-op on iOS: there's no 48-bit split, and the VA is tiny. |
| `FEX::Config::LoadConfig("bb-probe", environ)` | Linux config paths | Point it at the app sandbox, or set options in code. |
| `host_call_arm64.S` | ELF directives (`.type`, `.size`, `.note.GNU-stack`); symbol without `_` | Mach-O: `_gow3cpu_host_call`, no ELF directives. |
| **Darwin arm64 ABI** | AAPCS64: every stack argument takes an 8-byte slot | Apple packs stack arguments at **natural alignment** (an int32 takes 4 bytes). `CallHost` copies 8-byte slots, so any host import with more than 8 integer/pointer arguments **and** sub-64-bit stack arguments gets the wrong values. Audit these imports, or add a per-import signature. |
| x18 | not used (good) | x18 is reserved on Darwin; keep it that way, since fex-ios already avoids it. |
| TSC | `cntvct_el0`, `cntfrq_el0` | Both readable from EL0 on Apple (24 MHz); keep the SMALLTSCSCALE scaling. |
| AVX | `wide_xmm = AVX && SVE256` | Apple has no SVE, so this takes the same 128-bit path as Snapdragon. |

## B. Guest memory: `src/runtime_memory.c`
| Item | Linux code | iOS work |
|---|---|---|
| Address window | low host map from `0x08_0000_0000` (32 GiB); PS4 user range `0x10_0000_0000`–`0xFC_0000_0000` | **iOS standard VA ends at `0xF_C000_0000` (63 GiB)** (JITAllocator.h). The extended map (512 GiB) needs `com.apple.developer.kernel.extended-virtual-addressing`, which free Apple IDs can't sign. Either move the window down (low map ~8 GiB, user range ~16–56 GiB) and confirm the game never asks for fixed addresses outside it, or require the extended VA entitlement. **Check which hints the game passes to its `mmap` and `reserve` calls.** |
| Direct pool | `memfd_create` + `MAP_SHARED` views; `fallocate(PUNCH_HOLE)` | No memfd on iOS. Use `mach_make_memory_entry_64(MAP_MEM_NAMED_CREATE)` + `mach_vm_map` for aliased views, and `madvise(MADV_FREE_REUSABLE)` or re-mapping instead of punching holes. The Windows path's `memset` discard also works. |
| `MAP_FIXED_NOREPLACE` | low map | `mach_vm_allocate(..., VM_FLAGS_FIXED)` fails when the range is occupied, which gives the same behavior. |
| `host_prot` gives PROT_EXEC for PS4 X pages | | Remove EXEC (see A). On iOS a mapping that asks for X without the JIT pool fails. |
| Footprint | `GOW3_DMEM_MB` ≥ 5056 MiB of direct memory + 1 GiB flexible | iOS counts every **touched** page; there's no swap and jetsam kills over-limit apps. The base iPhone 17 has 8 GB RAM, and even with `increased-memory-limit` (a paid account may be required) the real limit is probably about 5–6 GB, **shared with the JIT pool, MoltenVK and GPU allocations**. Measure GoW3's resident set early. This is the most likely reason the port might not fit. |
| GPU page tracking | mprotect + SIGSEGV write faults | Works the same at 16 KB, but faults on XNU are slower (Mach exception → signal). Watch the fault rate; the existing `GOW3_FAULT_WINDOW` and hot-page options help. |

## C. Loader and services: `src/probe.c`, `src/runtime_*.c`
| Item | Linux code | iOS work |
|---|---|---|
| Entry | `int main(argc, argv)` in a separate process launched by run.py or the launcher | Change it to `gow3_start(const char *game_dir, ...)`, started on a thread inside the app. Replace argv flags and `GOW3_*` env vars with `setenv` from the shell's settings. |
| `_exit(...)` on fatal errors | | Ends the app. Acceptable at first; later, report the error to the shell first. |
| `runtime_restart` | `execl /bin/sh` / `bash run.sh` | Not possible. Return to the launcher, and tell the user to relaunch for a clean state. |
| Thread dump | `/proc/self/task` + `SYS_tgkill` | `task_threads()` + `pthread_kill`; diagnostics only. |
| `SYS_close_range` | restart path | Removed with restart. |
| `arch_prctl(ARCH_SET_GS)` (runtime_thread.c, runtime_cpu.c) | x86 native path only | Never built on ARM64 (`GUEST_CPU_NATIVE`). Nothing to do. |
| Signals: SIGSEGV/SIGBUS/SIGILL/SIGTRAP/SIGUSR2/SIGALRM | sigaction | Available on iOS. While a debugger is attached it gets the exception first; that's fine, because the debugger detaches after the pool is mapped. |
| Files | plain POSIX paths in the dump folder | Keep the dump in the app's Documents folder, imported through the Files app; saves go in Application Support. |
| Audio | SDL3 audio streams | SDL3 audio without SDL_main (call `SDL_SetMainReady`), or an AudioQueue / AVAudioEngine backend. Set the AVAudioSession category. |
| Pad | SDL3 gamepad (+ pad file / FIFO on Android) | GameController framework (DualSense / DualShock 4 / Backbone) behind the same `runtime_pad` interface; touch controls are a later step. |

## D. GPU: `gpu/shim`, `gpu/shadps4`
| Item | Linux code | iOS work |
|---|---|---|
| Window | `gpu/shim/window.cpp`: SDL3 window, X11/Wayland/Win32 handles | New `window_ios.mm`: the Expo native view gives us a CAMetalLayer, passed in `window_info.render_surface`. The vendored shadPS4 `vk_platform.cpp` already has an `__APPLE__` Metal-surface and portability path. Use the UIView size for the resolution and handle rotation. |
| Vulkan driver | Turnip (Adreno), loaded from the APK | MoltenVK.xcframework, linked statically (no Vulkan loader on iOS). |
| Feature coverage | GoW3 fixes are tuned for Turnip/RADV/NVIDIA | MoltenVK lacks or emulates geometry shaders, some formats, and some transform-feedback/MSAA behavior. `gow3_msaa_default.h` already mentions MoltenVK. Expect renderer fixes; **find this out early** with a Vulkan smoke test (`src/vulkan_smoke.c`) on the device. |
| Shader compile | sirit → SPIR-V | SPIR-V → MSL through MoltenVK's SPIRV-Cross. Keep the background pipeline cache, since the first run will stutter. |
| FSR / lsfg / DLSS bridge | | Drop at first (FSR1 might be kept later). |
| `xbyak` | x86 JIT inside shadPS4 | Not used on ARM64 hosts; make sure it is compiled out. |

## E. Build and distribution
- **Native libs** (GitHub Actions `macos-15`): a CMake iOS toolchain builds the FEXCore static libs (fex-ios), `libgow3runtime.a` (src/), `libgow3gpu.a` (gpu/), SDL3 (audio only) and MoltenVK.
- **App:** Expo prebuild → `xcodebuild CODE_SIGNING_ALLOWED=NO` → `Payload/*.app` zipped into `.ipa` → re-signed with SideStore.
- **Entitlements:** `get-task-allow` (JIT through a debugger), `com.apple.security.cs.allow-jit`, `com.apple.developer.kernel.increased-memory-limit`, and maybe `extended-virtual-addressing`.
  Madeira ships the first three. Whether a free account keeps the memory ones after re-signing needs checking.
- **Licenses:** gow3 code is GPL-2.0+, Madeira FEX changes are GPL-3.0+ (with the Madeira converter exception), StikJIT is MPL-2.0, and MoltenVK is Apache-2.0. Release the combined project as GPL-3.0 with all sources published.

## Open questions to settle first, in order
1. Does `jit_task_map_range` report 63 GiB or 512 GiB on the phone, under the signing we can actually get?
2. What resident set does GoW3 need? Measure it on Android or PC (`runtime_memory` live_bytes and RSS) with the 01.02 dump.
3. Does the dual-mapped FEXCore JIT run on the iPhone 17 with iOS 27? MaxPS4 reports a synthetic test passing on iOS 27.2.
4. Is MoltenVK on iOS enough for shadPS4's video core? Run the smoke test, then the title screen.
