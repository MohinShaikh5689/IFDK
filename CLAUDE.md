# gow3-ios

Goal: iOS port of the community God of War III Remastered (PS4) port, the same approach as gow3_android.
It is not an emulator and not a recompiled game. The game's own `eboot.bin` is loaded into the app process.
A game-specific HLE runtime replaces the PS4 system libraries. FEXCore translates the x86-64 code to ARM64.
shadPS4's video core turns the PS4 GPU work into Vulkan, which runs on MoltenVK and then Metal.
Users supply their own legally dumped game (CUSA01623 / CUSA01715 / CUSA01720, update 01.02). Never ship game files.

## Constraints
- Target device: base iPhone 17 (8 GB RAM) on iOS 27. Distributed by sideloading only (SideStore/AltStore); the App Store is not possible because JIT needs a debugger.
- JIT comes from StikDebug or a built-in StikJIT-style helper. On iOS 27 the helper can pair the phone without a computer, as Madeira does.
- iOS uses 16 KB pages. The gow3_android runtime says it needs 4 KB pages, so this is a known porting problem.
- One process only. iOS apps can't spawn processes.
- **There is no Mac.** All iOS builds run on GitHub Actions macOS runners and produce an unsigned IPA, which is re-signed with SideStore.
  Logs and crash reports come from Linux via libimobiledevice (`idevicesyslog`). Test CPU and runtime code on Linux ARM64 or under QEMU where possible.
- License: gow3 port is GPL-2.0-or-later and Madeira's FEX changes are GPL-3.0-or-later, so the combined result is GPL-3.0. Upstream FEX is MIT.

## Planned stack
```
Expo (React Native) app shell: launcher, dump import via Files app, settings, JIT status
└── local Expo native module (modules/gow3-core): Swift/ObjC++ bridge, prebuilt static libs
    └── fullscreen native UIViewController + CAMetalLayer while the game runs (RN stays idle behind it)
└── gow3 runtime (src/runtime_*.c, src/probe.c loader, patches/)   ← from gow3_android
    ├── standalone FEXCore + iOS JIT/16K fixes                     ← move them over from willfaust/FEX ios-port-2607
    └── shadPS4 video core → Vulkan → MoltenVK → Metal             ← replaces Turnip + AHardwareBuffer
```

## App shell decisions
- The shell is Expo / React Native, which the user chose. The native core is a local Expo module whose podspec vendors static libs built by CMake in an earlier CI step.
- CI: `npx expo prebuild -p ios`, then `xcodebuild` with `CODE_SIGNING_ALLOWED=NO`, then package `Payload/` as an unsigned `.ipa`. EAS Build isn't used because it wants Apple signing credentials.
- RN does not own the game's rendering. The game runs on its own threads and draws to a CAMetalLayer in a native view controller presented over RN.
  SDL3 can't own the UIApplication here, so the window comes from our own iOS window code. Input goes through the GameController framework, and audio through SDL audio without SDL_main, or AudioQueue as a fallback.

## Reference repos (`ref/`, shallow clones without submodules)
- `ref/gow3_android`: wyatt-poole/gow3_android, the base. `src/guest_cpu.h` is the single CPU boundary: plain calls on x86-64, FEXCore on ARM64.
  `README.android.md` documents the Android build: a Linux x86-64 host cross-compiling against a Debian arm64 sysroot.
  Other forks: zFitness/gow3_android, plus erickmendes22/gow3_android_daedalus_fork. The Daedalus fork uses Bionic and AHardwareBuffer instead of a glibc rootfs and Xvfb.
- `ref/fex-ios`: willfaust/FEX branch ios-port-2607, the iOS FEX fork Madeira uses. It runs as Wine's ARM64EC/WoW64 CPU module, not standalone.
  Fixes worth taking: dual-mapped JIT pool (code buffer is execute-only and writes go through `DualMap::WriteAddr`), self-modifying-code tracking at 16 KB page granularity, and x18 left unused (iOS zeroes it).
  See willfaust/FEX PR #1. Its forks are AI-assisted, so never send their changes upstream to FEX-Emu.
- `ref/Madeira`: willfaust/Madeira, which runs FEX + Wine + DXMT on jailed iOS 26+. Reference for JIT enabling, the single-process app shell and the IPA build.
- `ref/MaxPS4`: EnermaxFR/MaxPS4, an early PS4-on-iOS project that boots no games yet. Reference for building standalone FEXCore for Darwin with StikDebug JIT.
  Its owner reports a synthetic JIT test passing on iOS 27.2. Its GitHub Actions workflow produces a SideStore IPA.

## Milestones
1. Audit: list the Linux/Android-specific code in gow3 `cpu/` and `src/` (mmap/mprotect flags, signals/ucontext, futex, /proc, page-size assumptions, the Xvfb/SDL display path).
   Map each item to the iOS fix that already exists in fex-ios, MaxPS4 or Madeira. DONE: see docs/AUDIT.md (including its open questions).
2. GitHub Actions: build a minimal iOS app plus standalone FEXCore and produce a sideloadable IPA.
   WRITTEN, NOT YET BUILT. `.github/workflows/ios.yml`, `native/scripts/build-ios.sh`, and the Expo app with `modules/gow3-core`.
   Nothing iOS-specific can be compiled on this Linux machine, so expect a few CI fix-up rounds.
3. Get FEXCore running a small x86-64 test function on the iPhone with JIT enabled.
   WRITTEN: `native/selftest`. On device: Enable JIT, then Run self-test. Also measures the address map, the jetsam budget,
   the no-footprint ledger trick and named-entry aliasing.

## Native core layout
- `native/jit/jit_pool.*`: pool from `brk #0xf00d`, x16=1 (debugger `_M size,rx`), RW alias by `vm_remap`, first-fit carve.
  FEXCore's `Allocator::mmap`/`munmap` hooks route PROT_EXEC requests here, and `DualMap::WriteOffset` = rw − rx.
- `native/cpu/guest_cpu_fex_darwin.cpp`: Darwin port of gow3_android `cpu/guest_cpu_fex.cpp`. It uses `__ss`/`__ns` signal state,
  accepts SIGBUS as a fault signal, uses 16 KiB guards, `vm_read_overwrite`, and never maps guest code executable.
- `native/cpu/host_features_darwin.cpp`: FEX host features from sysctl. FEX's `Source/Common` isn't built on Apple,
  and its ID-register reads would SIGILL on iOS. AFP is off and AVX is emulated, as in Madeira.
- FEX config: only `FEXCore::Config::Initialize/Load/ReloadMetaLayer/Set`. No file or env layers, since those live in Common.
- The build injects `gow3core` into FEX's own CMake (`CMAKE_PROJECT_FEX_INCLUDE`), then `libtool -static` merges all archives
  into `app/modules/gow3-core/ios/lib/libgow3core_all.a`, which the podspec vendors.
- Swift reaches the C API (`native/include/gow3core.h`) through `@_silgen_name`, without a bridging header.
4. Build the gow3 runtime and loader for iOS, testing on Linux ARM64 first.
5. Get MoltenVK rendering with shadPS4's video core, then boot GoW3.
6. Tune memory to fit 8 GB, and tune performance and thermals.
