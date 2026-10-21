# Built-in JIT (no StikDebug)

Works on the iPhone 17 (iOS 27, AltStore install) since app 0.1.4 (2026-10-21).

iOS only lets a sideloaded app make memory executable while a debugger has marked it `CS_DEBUGGED`.
gow3-ios brings its own debugger client, the same design as Madeira's "Built-in StikJIT":

- **Helper extension** `PlugIns/Gow3JITHelper.appex` (`native/jit-helper/`): a classic app extension on
  `com.apple.ar.viewer`, because a process cannot debug itself. It links StikJIT 1.9.0 (MPL-2.0, contains
  idevice, MIT) from `Frameworks/StikJIT.framework`. It reaches the iPhone's own developer services
  through LocalDevVPN's loopback (`10.7.0.1`), mounts the Developer Disk Image when needed, attaches to the
  app and runs StikJIT's universal script (`app/modules/gow3-core/ios/jit/universal.js`). The app's
  `gow3_jit_setup` then maps the JIT pool, and the script detaches.
- **In-app pairing** (iOS 27): `native/rppairing` (Rust, idevice's `PairableHost`, from Madeira) plus
  `Gow3Pairing.swift`. The app advertises itself as a computer (`_remotepairing-pairable-host._tcp`).
  You tap **Pair with gow3-ios** under Settings > Privacy & Security > Developer Mode and enter the
  code the app shows; the code is also posted as a notification. The pairing file is kept in the Keychain,
  never in Documents.
- The app starts the helper by the bundle ID in its own Info.plist (`<app id>.JITHelper`), so it still
  works after AltStore/SideStore add their team ID to both IDs.

## Use
1. Install the IPA (AltStore keeps the memory entitlement; keep app extensions if it asks).
2. Install LocalDevVPN (App Store) and turn on Developer Mode. Stay on Wi-Fi: the loopback does not
   work while cellular data is in use.
3. **Pair in app**, then go to Settings > Privacy & Security > Developer Mode > **Pair with gow3-ios**,
   and type the code. Each pairing replaces the last one.
4. Connect LocalDevVPN, tap **Check setup** once (downloads and mounts the Developer Disk Image), then
   **Enable JIT**.

StikDebug still works as a fallback: **Enable JIT** uses it when the app is not paired.

## Known limits
- iOS 26+ for the helper, iOS 27+ for in-app pairing. Not inside LiveContainer.
- When a sideloader renames the bundle ID, iOS may refuse the background task that keeps pairing alive
  (`<bundle id>.pairing.session`). Then the app only has about 30 s in the background, so go to Settings
  straight away.
- CI builds the helper with `swiftc` against StikJIT's `.swiftinterface` (made by Swift 6.4).
