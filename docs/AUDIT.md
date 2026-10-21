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
| `guest_cpu_hook` RIP changes | Upstream FEX (gow3_android) | In willfaust/FEX, `syscall` ends a JIT block only on `_WIN32` (`DEFAULT_SYSCALL_FLAGS`), so a hook that sets RIP is ignored. Before the game's hooks are used, give SYSCALL `FLAGS_BLOCK_END` on Apple (a small FEX patch). Host stubs don't need it. |
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

## Device results (self-test, 2026-10-20)
Device: iPhone 17, `iPhone18,3`, iOS build 24A435, 7672 MB RAM, free Apple ID, direct SideStore install.
- Task map is `[0x102344000, 0x7180000000)` (449 GB), with 16 KiB pages.
- **Jetsam limit is about 3375 MB** (footprint plus `limit_bytes_remaining`). This is the binding constraint, since GoW3 has a 5 GB PS4 direct pool.
- Named memory entries alias correctly, and a fixed remap of a released view works, so this can replace memfd.
- `mach_memory_entry_ownership(..., NO_FOOTPRINT)` fails (KERN_NO_ACCESS with the self owner on the first build, KERN_INVALID_ARGUMENT (4) with self and TASK_NULL on later ones) and the memory still counts. Dead end.
- **File-backed MAP_SHARED memory stays out of phys_footprint**: 512 MB of dirty pages in a mapped sandbox file added 0 MB, and two views of the file alias.
  This is the way past the jetsam limit for the direct pool (replaces memfd; pages may be written back to flash under pressure).
- **How Madeira gets past the limit** (checked 2026-10-21 in willfaust/Madeira). It uses two things, and does not lift the cap itself:
  1. It ships `com.apple.developer.kernel.increased-memory-limit`, and its code calls the result a "4096MB jetsam ceiling".
     Our 3375 MB was measured without that entitlement. App 0.1.1 adds it, but the 0.1.1-era run still showed about 3.3 GB.
     Madeira's own setup guide says free accounts can't provision the kernel memory keys through SideStore. It uses GetMoreRam,
     which adds `increased-memory-limit` and `extended-virtual-addressing` to the App ID, and then the same IPA is reinstalled.
     Extended VA would also fix the address-space problem (8 GB+ reservations fail today). App 0.1.2 asks for both keys,
     and the device report shows what the signature and the profile carry (`ent_*`, `profile_*`).
  2. A file-backed "swap tier" (ml1077, in Wine's `virtual_ios.c`; `swap-mb = 3072` in its example config).
     Guest commits go to MAP_SHARED mappings of a sparse `tmp/madeira-swap.bin`. The file is created with `NSFileProtectionNone`,
     so the mapping still works while the phone is locked. Decommit punches holes with `fcntl(F_PUNCHHOLE)`, and `swap-mb` is a soft cap on disk use.
     The "broad" mode backs every reservation of 4 MB or more, and Madeira says the device-verified case is Metro 2033 Redux.
     Its canaries found two costs. Dirtying a lot at once gets throttled by writeback (ml1080), and a first touch can stall behind writeback of the same file (ml1079), so separate files help.
     Its churn filter keeps short-lived block sizes anonymous.
  Madeira's NO_FOOTPRINT ownership calls fail the same way ours do (kr=4), which confirms that route is a dead end.
  Plan for GoW3: put the 5 GB direct pool in file-backed memory (one or more files in tmp) and keep anonymous memory for the JIT pool, FEX, MoltenVK and heaps, under the ~4 GB ceiling.
- **Free address space is only about 6-7 GB.** iOS itself reserves `0x1000000000+384G` (tag 31, prot 0) and `0x2e6000000+51G` (tag 32), plus 1 GB tag-32 regions.
  Same on a direct install, so it is not LiveContainer. ANYWHERE reservations: 4 GB ok (at `0x7000000000`, inside a 6 GB hole), 8 GB and larger fail.
  Fixed reservations at 32 GiB, 64 GiB and 256 GiB fail (KERN_NO_SPACE). The PS4 memory windows must be relocated and kept small.
- **JIT and FEXCore work (2026-10-20, StikDebug, native SideStore install).** Pool 256 MiB at `0x112400000`, RW alias via vm_remap.
  native JIT returns 42. FEX tests: plain return, arguments and a host call all give 42, and the 200M-iteration loop is correct
  (71 ms translated vs 112 ms for the unoptimized native C loop, so the JIT output is good). FEX init uses about 16 MiB of the pool.
  The first failure was ours: the pool put a PROT_NONE guard on the last RW page of every buffer, which was all of the 16 KiB dispatcher buffer.
- FEX's temporary compile buffer guard (`PooledAllocatorVirtualWithGuard`) protects one 4 KiB page, which 16 KiB host pages cannot do exactly.
  Not hit yet; patch it to a host page before running real game code.

## Open questions to settle first, in order
1. Does `jit_task_map_range` report 63 GiB or 512 GiB on the phone, under the signing we can actually get?
2. What resident set does GoW3 need? Measure it on Android or PC (`runtime_memory` live_bytes and RSS) with the 01.02 dump.
3. ~~Does the dual-mapped FEXCore JIT run on the iPhone 17 with iOS 27?~~ Yes (see Device results).
4. Is MoltenVK on iOS enough for shadPS4's video core? Run the smoke test, then the title screen.
