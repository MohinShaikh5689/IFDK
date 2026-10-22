# God of War III on iPhone: gow3-ios, a native iOS port of the PS4 game

**gow3-ios** brings **God of War III Remastered (PS4)** to the **iPhone**, without jailbreak, by
sideloading. It is a native port, not a PS4 emulator: the game's own `eboot.bin` runs inside the
app, its x86-64 code is translated to ARM64 by **FEXCore** (JIT), a game-specific runtime replaces
the PS4 system libraries, and **shadPS4's video core** renders through **Vulkan on Metal (MoltenVK)**.
It is the iOS counterpart of [gow3_android](https://github.com/wyatt-poole/gow3_android), which is
based on the PC port [gow3_pc](https://github.com/FelipePrado0/gow3_pc).

> **Status: in development, not playable yet.** On an iPhone 17 the game boots through FEXCore with
> JIT, the runtime loads it, and the GPU (MoltenVK) initializes. Getting to the title screen is the
> current milestone. See [Project status](#project-status).
>
> **No game files are included, and none will be.** You need your own legal dump of the game.

## Contents
- [Features](#features)
- [Project status](#project-status)
- [Requirements](#requirements)
- [Install on iPhone (sideload)](#install-on-iphone-sideload)
- [Enable JIT](#enable-jit)
- [Add your game files](#add-your-game-files)
- [FAQ](#faq)
- [Build from source (no Mac needed)](#build-from-source-no-mac-needed)
- [How it works](#how-it-works)
- [Credits and license](#credits-and-license)

## Features
- **Runs the original PS4 game code** on iOS through the FEXCore x86-64 JIT (no recompilation, no emulator).
- **Built-in JIT**: pair the iPhone with the app itself on iOS 27 (no computer, no StikDebug needed);
  StikDebug works too. See [docs/JIT.md](docs/JIT.md).
- **Vulkan on Metal** via MoltenVK, with shadPS4's GCN shader recompiler and Liverpool GPU emulation.
- **Sideloadable IPA built in the cloud** on GitHub Actions; no Mac required at any step.
- **Fits an 8 GB iPhone**: guest memory is file-backed, so it does not count against the app's memory limit.
- **The PC port's game patches and settings menu**, from gow3_pc and gow3_android.

## Project status
| Area | State |
|---|---|
| JIT on iPhone (built-in pairing or StikDebug) | Works (iPhone 17, iOS 27) |
| FEXCore running x86-64 game code | Works: modules load, the game's main code runs, threads start |
| PS4 runtime (kernel, memory, files, threads) | Works CPU-only up to the first GPU call |
| GPU: MoltenVK device, swapchain, buffer cache | Initializes (A19 GPU, Vulkan 1.4, Metal 4); first frames are next |
| Title screen / gameplay | Not yet |
| Audio, controllers, cutscene video | Not yet (next after first frames) |

Progress notes: [docs/NEXT.md](docs/NEXT.md). Platform findings: [docs/AUDIT.md](docs/AUDIT.md).

## Requirements
- **iPhone with 8 GB RAM** (developed on the base **iPhone 17**), **iOS 26 or newer** (iOS 27 for
  in-app pairing).
- **AltStore** to install (SideStore drops the increased-memory-limit entitlement the game needs).
- **JIT**: the built-in helper with LocalDevVPN (App Store), or [StikDebug](https://github.com/StikDebug/StikDebug).
- **Your own dump of God of War III Remastered**: CUSA01623, CUSA01715 or CUSA01720, with update 01.02
  (about 35 GB free on the iPhone).
- For now, `boot-linked.bin` from gow3's `scripts/prepare.py`, run on a PC.

## Install on iPhone (sideload)
1. Download the IPA: Actions tab, **iOS IPA** workflow, latest successful run, artifact
   `gow3-ios-Release-ipa`.
2. Install it with **AltStore** (keep app extensions if asked). With a free Apple ID, refresh it every 7 days.
3. Turn on **Developer Mode** (Settings > Privacy & Security).

## Enable JIT
iOS only allows JIT for an app a debugger has attached to. gow3-ios brings its own:
1. Install **LocalDevVPN** and stay on Wi-Fi.
2. In the app, **Pair in app**, then Settings > Privacy & Security > Developer Mode >
   **Pair with gow3-ios**, and type the code shown.
3. Connect LocalDevVPN, tap **Check setup** once, then **Enable JIT**.

Details and limits: [docs/JIT.md](docs/JIT.md).

## Add your game files
Copy into **Files > On My iPhone > gow3-ios > gow3**:
- `boot-linked.bin`, `content.bin`, `patches.bin` (from gow3's `out/` folder on the PC);
- `game/`: the whole dumped game folder (`eboot.bin`, `sce_module/`, `sce_sys/`, `EXEC/`, `ENGLISH/`,
  `*.plt`) with the 01.02 update's `.plt` files on top.

The game folder is about 30 GB. From Linux, `ifuse --documents <bundle id>` mounts the app's folder
(libimobiledevice); from Windows, iMazing works. Then tap **Start game**.

## FAQ
**Can I play God of War 3 on iPhone with this?**
Not yet. The port boots the game's code and initializes the GPU on an iPhone 17, but it does not show
the title screen yet.

**Is this a PS4 emulator for iOS?**
No. It runs one game: the PS4 system libraries are replaced by a runtime written for God of War III,
and only the game's x86-64 instructions are translated (FEXCore). shadPS4's video core handles graphics.

**Do I need a jailbreak?**
No. It is a sideloaded app (AltStore). JIT comes from a debugger attach, done by the app's built-in
helper or StikDebug.

**Why is it not on the App Store?**
App Store apps cannot use JIT, and the game cannot run without it.

**Does it include the game?**
No, and it never will. Dump your own copy of the game you own.

**Which iPhones will work?**
Only the iPhone 17 (8 GB) has been tested. Other 8 GB iPhones with a recent Apple GPU (iPhone 15 Pro, 16 series) may work but are untested.
4-6 GB iPhones are not expected to fit the game.

**Is there an Android or PC version?**
Yes: [gow3_android](https://github.com/wyatt-poole/gow3_android) and [gow3_pc](https://github.com/FelipePrado0/gow3_pc).

## Build from source (no Mac needed)
Everything builds on GitHub Actions macOS runners:
- **iOS IPA** (`.github/workflows/ios.yml`): FEXCore, the runtime, the GPU library, MoltenVK and SDL3
  (`native/scripts/build-ios.sh`), then `expo prebuild` and an unsigned `xcodebuild`; the IPA is the
  `gow3-ios-Release-ipa` artifact. Run it from the Actions tab (workflow_dispatch) on any branch.
- **GPU library** (`.github/workflows/gpu.yml`): compiles shadPS4's video core for iOS and lists errors.
- **Runtime** (`.github/workflows/runtime.yml`): Linux tests and an iOS compile check.

The JavaScript app ships by EAS Update (`expo-updates`, channel `production`).

## How it works
```
Expo (React Native) app: launcher, JIT setup, game files, logs
└── native module (Swift) + libgow3core_all.a
    ├── gow3 runtime and loader (native/runtime/src)   PS4 kernel, memory, files, threads (HLE)
    ├── FEXCore (iOS fork) + JIT pool (native/cpu, native/jit)   x86-64 -> ARM64
    └── GPU library (native/runtime/gpu)   shadPS4 video core -> Vulkan -> MoltenVK -> Metal
```

| Path | What it is |
|---|---|
| `app/` | Expo shell; `app/modules/gow3-core` is the Swift native module. |
| `native/runtime/src` | gow3_android's runtime and loader, ported to Darwin. |
| `native/runtime/gpu` | gow3_android's GPU library (shadPS4 video core), ported to iOS/MoltenVK. |
| `native/cpu`, `native/jit` | `guest_cpu.h` on FEXCore for Darwin; the dual-mapped JIT pool. |
| `native/jit-helper`, `native/rppairing` | Built-in JIT helper extension and iOS 27 in-app pairing. |
| `native/third_party/FEX` | [willfaust/FEX](https://github.com/willfaust/FEX) `ios-port-2607`. |

## Credits and license
gow3-ios is **GPL-3.0-or-later** (see `LICENSE`). It builds on:
- [gow3_android](https://github.com/wyatt-poole/gow3_android) and [gow3_pc](https://github.com/FelipePrado0/gow3_pc)
  (GPL-2.0-or-later): runtime, loader, GPU library and patches;
- [shadPS4](https://github.com/shadps4-emu/shadPS4) (GPL-2.0-or-later): video core and shader recompiler;
- [FEX](https://github.com/FEX-Emu/FEX) (MIT) and [willfaust/FEX](https://github.com/willfaust/FEX)'s iOS
  changes (GPL-3.0-or-later); [Madeira](https://github.com/willfaust/Madeira) for the iOS JIT approach;
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK) (Apache-2.0), [SDL3](https://github.com/libsdl-org/SDL) (Zlib),
  StikJIT (MPL-2.0), idevice (MIT).

Not affiliated with Sony Interactive Entertainment or Santa Monica Studio. God of War is a trademark
of Sony Interactive Entertainment. This project contains no copyrighted game data.
