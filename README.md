# gow3-ios

An iOS port of the community God of War III Remastered (PS4) port
([gow3_android](https://github.com/wyatt-poole/gow3_android)). The game's own x86-64 code runs through
FEXCore's JIT, a game-specific runtime replaces the PS4 system libraries, and shadPS4's video core
renders through Vulkan on Metal. **You need your own legally dumped copy of the game; this repository
contains no game files and never will.**

Status: **milestone 2.** The app enables JIT and runs a self-test. That test reports what this iPhone
allows (address space, memory limits, memory tricks) and runs x86-64 code through FEXCore. The game
does not run yet. See [docs/AUDIT.md](docs/AUDIT.md) for the porting plan.

## Layout
| Path | What it is |
|---|---|
| `app/` | Expo (React Native) shell. `app/modules/gow3-core` is the native module, written in Swift. |
| `native/jit/` | The JIT pool: debugger-prepared RX memory with an RW alias (StikDebug universal protocol). |
| `native/cpu/` | `guest_cpu.h` on FEXCore for Darwin, ported from gow3_android. |
| `native/selftest/` | The on-device self-test behind the app's buttons. |
| `native/third_party/FEX` | [willfaust/FEX](https://github.com/willfaust/FEX) `ios-port-2607` (Madeira's iOS FEX fork). |
| `.github/workflows/ios.yml` | Builds the IPA on a macOS runner. No Mac is needed. |

## Getting the IPA
Push to GitHub. The **iOS IPA** workflow builds the native core, runs `expo prebuild` and `xcodebuild`,
and uploads `gow3-ios.ipa` as the `gow3-ios-ipa` artifact. You can also start it by hand from the
Actions tab (workflow_dispatch).

## Installing and running (free Apple ID)
1. Install the IPA with **SideStore** (or AltStore / Sideloadly). A free Apple ID must re-sign every 7 days.
2. Install **[StikDebug](https://github.com/StikDebug/StikDebug)**, import your iPhone's pairing file, and
   connect **LocalDevVPN**.
3. Open gow3-ios and tap **Enable JIT**. StikDebug opens, attaches to the app and runs the bundled JIT
   script. Come back to gow3-ios: the app asks it for a 256 MiB JIT pool, then lets it detach.
4. Tap **Run self-test** and share the JSON and the log. The log survives a crash, so if the app
   dies, reopen it: the last line shows the step that failed.

What the self-test answers:
- `va_max`, `va_gb`: the task address map. The PS4 runtime's memory window has to fit inside it.
- `footprint_limit_mb`, `available_mb`: the jetsam budget without the increased-memory entitlement.
- `nofootprint_256_footprint_delta_mb` versus `plain_256_…`: whether memory with the private
  no-footprint ledger flag stays out of that budget. If it does, the 5 GB PS4 pool might fit on an 8 GB phone.
- `alias_ok`: whether named memory entries alias correctly. They replace Linux's memfd for PS4 direct memory.
- `window_*_kr`: whether fixed reservations at 32 GiB, 64 GiB and 256 GiB succeed (0 = yes).
- `native_jit_42`, `fex_return_42`, `fex_args_42`, `fex_host_call_42`, `fex_loop_ok`: JIT and
  FEXCore work, and the guest can call into the host.
- `fex_loop_ms` versus `native_loop_ms`: a first measure of translation overhead.

## License
GPL-3.0-or-later (see `LICENSE`). Third-party code keeps its own license:
- gow3_android code (`native/cpu/guest_cpu.h`, `host_call_arm64.S`, `guest_cpu_fex_darwin.cpp`): GPL-2.0-or-later.
- FEX: MIT. Madeira's changes to it: GPL-3.0-or-later.
- StikJIT's `universal.js`: MPL-2.0 (`app/modules/gow3-core/ios/jit/`).
