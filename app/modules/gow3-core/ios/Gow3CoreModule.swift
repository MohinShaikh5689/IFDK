// SPDX-License-Identifier: GPL-3.0-or-later
// The Expo bridge to the native core (native/include/gow3core.h, linked from lib/libgow3core_all.a).
import ExpoModulesCore
import GameController
import Metal
import UIKit

@_silgen_name("gow3_device_report")
private func gow3_device_report(_ out: UnsafeMutablePointer<CChar>?, _ capacity: Int) -> Int
@_silgen_name("gow3_jit_debugged")
private func gow3_jit_debugged() -> Int32
@_silgen_name("gow3_jit_debugger_attached")
private func gow3_jit_debugger_attached() -> Int32
@_silgen_name("gow3_jit_setup")
private func gow3_jit_setup(_ poolMb: UInt32) -> Int32
@_silgen_name("gow3_start")
private func gow3_start(_ argc: Int32, _ argv: UnsafePointer<UnsafePointer<CChar>?>?, _ logPath: UnsafePointer<CChar>?) -> Int32
@_silgen_name("gow3_dump_threads")
private func gow3_dump_threads() -> Int32
@_silgen_name("gow3_jit_pool_ready")
private func gow3_jit_pool_ready() -> Int32
@_silgen_name("gow3_gpu_screen_test")
private func gow3_gpu_screen_test(_ layer: UnsafeMutableRawPointer?, _ width: UInt32, _ height: UInt32, _ seconds: UInt32,
                                  _ out: UnsafeMutablePointer<CChar>?, _ capacity: Int) -> Int
@_silgen_name("gow3_gpu_set_surface")
func gow3_gpu_set_surface(_ layer: UnsafeMutableRawPointer?, _ width: UInt32, _ height: UInt32, _ refreshHz: UInt32)
@_silgen_name("gow3_selftest")
private func gow3_selftest(_ logPath: UnsafePointer<CChar>?, _ out: UnsafeMutablePointer<CChar>?, _ capacity: Int) -> Int

private func readJson(_ call: (UnsafeMutablePointer<CChar>?, Int) -> Int) -> String {
  var buffer = [CChar](repeating: 0, count: 64 * 1024)
  _ = buffer.withUnsafeMutableBufferPointer { call($0.baseAddress, $0.count) }
  return String(cString: buffer)
}

private struct Gow3Error: Error, CustomStringConvertible {
  let description: String
}

/// Settles a promise once, from whichever of the debugger wait and the helper's answer comes first.
private final class OneShot {
  private let lock = NSLock()
  private var done = false
  /// True for the first caller only.
  func claim() -> Bool {
    lock.lock()
    defer { lock.unlock() }
    if done { return false }
    done = true
    return true
  }
  var isDone: Bool {
    lock.lock()
    defer { lock.unlock() }
    return done
  }
}

/// Waits up to `timeout` for a debugger, then has it prepare a `poolMb` MiB JIT pool and detach.
/// `finish` gets gow3_jit_setup's result, or nil on timeout or when `shot` was claimed elsewhere.
private func waitForDebuggerThenSetup(poolMb: Int, timeout: TimeInterval, shot: OneShot, finish: @escaping (Int32?) -> Void) {
  DispatchQueue.global(qos: .userInitiated).async {
    let deadline = Date().addingTimeInterval(timeout)
    while gow3_jit_debugger_attached() == 0 {
      if shot.isDone { return }
      if Date() > deadline {
        finish(nil)
        return
      }
      Thread.sleep(forTimeInterval: 0.25)
    }
    finish(gow3_jit_setup(UInt32(poolMb)))
  }
}

public class Gow3CoreModule: Module {
  private static var logURL: URL {
    FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("selftest.log")
  }

  private static var universalScript: Data? {
    let bundles = [Bundle(for: Gow3CoreModule.self), Bundle.main]
    for bundle in bundles {
      if let resources = bundle.url(forResource: "Gow3Core", withExtension: "bundle"),
         let script = Bundle(url: resources)?.url(forResource: "universal", withExtension: "js"),
         let data = try? Data(contentsOf: script) {
        return data
      }
    }
    return nil
  }

  /// Documents/gow3: boot-linked.bin, content.bin and patches.bin from a PC install of gow3 (its out/
  /// folder) and game/, the dumped game folder (eboot.bin, sce_sys, ...). Copied in with the Files app.
  private static var gameRoot: URL {
    FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("gow3")
  }

  private static var gameLogURL: URL { gameRoot.appendingPathComponent("loader.log") }

  private static var gameStarted = false

  /// JIT and the game files are ready, and the loader has not run yet (it runs once per launch).
  private static func checkCanStart() throws {
    guard !gameStarted else { throw Gow3Error(description: "The game already started; relaunch the app to start it again.") }
    guard gow3_jit_pool_ready() != 0 else { throw Gow3Error(description: "Enable JIT first.") }
    let manager = FileManager.default
    guard manager.fileExists(atPath: gameRoot.appendingPathComponent("boot-linked.bin").path) else {
      throw Gow3Error(description: "Missing Documents/gow3/boot-linked.bin (from gow3's out/ folder on the PC).")
    }
    guard manager.fileExists(atPath: gameRoot.appendingPathComponent("game/eboot.bin").path) else {
      throw Gow3Error(description: "Missing Documents/gow3/game/eboot.bin (the dumped game folder).")
    }
  }

  /// The dumped game (about 32 GB) and the files made from it on a PC can be copied in again, so
  /// they stay out of iCloud and computer backups; user/ (the saves) is still backed up.
  private static func excludeGameFilesFromBackup() {
    var values = URLResourceValues()
    values.isExcludedFromBackup = true
    for name in ["game", "boot-linked.bin", "content.bin", "patches.bin"] {
      var url = gameRoot.appendingPathComponent(name)
      if FileManager.default.fileExists(atPath: url.path) { try? url.setResourceValues(values) }
    }
  }

  private static var thermalObserver: NSObjectProtocol?

  /// Thermal state changes go to the game log, to line them up with what the game was doing.
  private static func watchThermalState(log: String) {
    func note(_ state: ProcessInfo.ThermalState) {
      let name: String
      switch state {
      case .nominal: name = "nominal"
      case .fair: name = "fair"
      case .serious: name = "serious"
      case .critical: name = "critical"
      @unknown default: name = "unknown"
      }
      try? "--- \(Date()) thermal state: \(name)\n".appendLine(to: log)
    }
    note(ProcessInfo.processInfo.thermalState)
    thermalObserver = NotificationCenter.default.addObserver(
      forName: ProcessInfo.thermalStateDidChangeNotification, object: nil, queue: nil) { _ in
      note(ProcessInfo.processInfo.thermalState)
    }
  }

  /// Documents/gow3/env.txt: KEY=VALUE lines (# starts a comment) set as environment variables before
  /// the loader starts, ahead of the defaults below. For diagnostics and FEX options
  /// (GOW3_GPU_PROFILE=1, FEX_DISABLEL2CACHE=0, ...) without a new build.
  private static func applyEnvFile() -> [String] {
    guard let text = try? String(contentsOf: gameRoot.appendingPathComponent("env.txt"), encoding: .utf8) else { return [] }
    var applied: [String] = []
    for line in text.split(whereSeparator: \.isNewline) {
      let entry = line.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false)[0]
        .trimmingCharacters(in: .whitespaces)
      guard let equals = entry.firstIndex(of: "="), equals != entry.startIndex else { continue }
      let key = entry[..<equals].trimmingCharacters(in: .whitespaces)
      let value = entry[entry.index(after: equals)...].trimmingCharacters(in: .whitespaces)
      setenv(key, value, 1)
      applied.append("\(key)=\(value)")
    }
    return applied
  }

  /// Runs the loader (gow3_start) on its own thread with the game in Documents/gow3.
  private static func startLoader(cpuOnly: Bool) throws {
    try checkCanStart()
    let envFile = applyEnvFile()
    let root = gameRoot
    let manager = FileManager.default
    let boot = root.appendingPathComponent("boot-linked.bin").path
    let game = root.appendingPathComponent("game").path
    let user = root.appendingPathComponent("user").path
    try? manager.createDirectory(atPath: user, withIntermediateDirectories: true)
    var args = ["gow3", boot] + (cpuOnly ? ["--cpu-only"] : []) + ["--app0", game, "--user", user, "--timeout", "0"]
    for (option, file) in [("--content-profile", "content.bin"), ("--patches", "patches.bin")] {
      let path = root.appendingPathComponent(file).path
      if manager.fileExists(atPath: path) { args += [option, path] }
    }
    // The guest memory pool is a sparse file: in tmp, which iOS does not back up.
    setenv("GOW3_POOL_DIR", NSTemporaryDirectory(), 1)
    // The GPU library's settings (gow3.ini, written by its menu) next to the saves; the working
    // directory is read-only on iOS.
    setenv("GOW3_CONFIG", user + "/gow3.ini", 0)
    // Direct memory: retail 5056 MiB + the 2048 MiB that gow3's default patch (Texture Corruption Fix,
    // a larger Video Arena) asks for, as run.py derives it on the PC. With 5056 the game's 2.45 GB
    // video arena allocation fails and it dereferences the missing pointer. The pool is file-backed,
    // so this costs no app memory until pages are written.
    setenv("GOW3_DMEM_MB", "7104", 0)
    // With graphics, no new frame for 60 s dumps the threads to the log, and 180 s ends the app
    // (gow3_start.c), so a hung game does not keep the CPU busy and the phone hot.
    if !cpuOnly { setenv("GOW3_FRAME_WATCHDOG", "60", 0) }
    // Flip rate every 5 s and a breakdown of each frame over 40 ms (faults, compiles, waits), while
    // the frame rate is being worked on.
    if !cpuOnly { setenv("GOW3_FRAME_STATS", "1", 0) }
    // Documents/gow3/toggles.txt: a number whose bits switch GPU and memory optimizations off,
    // re-read every 250 ms while the game runs (runtime_memory.c, gpu/shim/gow3_toggles.h), so a
    // rendering problem can be narrowed down by swapping the file in the Files app. No file: 0.
    setenv("GOW3_TOGGLE_FILE", root.appendingPathComponent("toggles.txt").path, 0)
    excludeGameFilesFromBackup()
    let log = gameLogURL.path
    watchThermalState(log: log)
    try? "--- \(Date()) start: \(args.joined(separator: " "))\n".appendLine(to: log)
    if !envFile.isEmpty { try? "--- env.txt: \(envFile.joined(separator: " "))\n".appendLine(to: log) }
    let cStrings = args.map { strdup($0) }
    defer { cStrings.forEach { free($0) } }
    var argv: [UnsafePointer<CChar>?] = cStrings.map { UnsafePointer($0) }
    argv.append(nil)
    let code = argv.withUnsafeBufferPointer { gow3_start(Int32(args.count), $0.baseAddress, log) }
    guard code == 0 else { throw Gow3Error(description: "The loader did not start (\(code)).") }
    gameStarted = true
  }

  public func definition() -> ModuleDefinition {
    Name("Gow3Core")

    // The native report plus what the GPU supports: PS4 textures are BC1-BC7, and without BC support
    // they would have to be transcoded (to ASTC) or expanded to RGBA (4-8x the memory).
    Function("deviceReport") { () -> String in
      let json = readJson { gow3_device_report($0, $1) }
      guard let gpu = MTLCreateSystemDefaultDevice(),
            var report = (try? JSONSerialization.jsonObject(with: Data(json.utf8))) as? [String: Any] else { return json }
      report["gpu"] = gpu.name
      report["bc_textures"] = gpu.supportsBCTextureCompression
      report["gpu_apple9"] = gpu.supportsFamily(.apple9)
      report["gpu_max_buffer_mb"] = gpu.maxBufferLength >> 20
      report["gpu_working_set_mb"] = gpu.recommendedMaxWorkingSetSize >> 20
      guard let data = try? JSONSerialization.data(withJSONObject: report, options: [.sortedKeys]) else { return json }
      return String(decoding: data, as: UTF8.self)
    }

    Function("isDebuggerAttached") { () -> Bool in
      gow3_jit_debugger_attached() != 0
    }

    Function("isCsDebugged") { () -> Bool in
      gow3_jit_debugged() != 0
    }

    AsyncFunction("isStikDebugInstalled") { () -> Bool in
      guard let url = URL(string: "stikdebug://enable-jit") else { return false }
      return UIApplication.shared.canOpenURL(url)
    }.runOnQueue(.main)

    // Opens StikDebug with the universal JIT script for this process, waits for the debugger, then
    // has it prepare a `poolMb` MiB JIT pool and detach. Resolves with gow3_jit_setup's result.
    AsyncFunction("enableJit") { (poolMb: Int, promise: Promise) in
      let start = {
        waitForDebuggerThenSetup(poolMb: poolMb, timeout: 120, shot: OneShot()) { code in
          if let code {
            promise.resolve(Int(code))
          } else {
            promise.reject(Gow3Error(description: "StikDebug did not attach within 120 s"))
          }
        }
      }
      if gow3_jit_debugger_attached() != 0 {
        start()
        return
      }
      guard let bundleId = Bundle.main.bundleIdentifier, let script = Gow3CoreModule.universalScript else {
        promise.reject(Gow3Error(description: "bundle id or JIT script missing"))
        return
      }
      var components = URLComponents()
      components.scheme = "stikdebug"
      components.host = "enable-jit"
      components.queryItems = [
        URLQueryItem(name: "bundle-id", value: bundleId),
        URLQueryItem(name: "pid", value: String(getpid())),
        URLQueryItem(name: "script-data", value: script.base64EncodedString()),
      ]
      guard let url = components.url else {
        promise.reject(Gow3Error(description: "cannot build the StikDebug URL"))
        return
      }
      UIApplication.shared.open(url, options: [:]) { opened in
        if opened {
          start()
        } else {
          promise.reject(Gow3Error(description: "StikDebug is not installed"))
        }
      }
    }.runOnQueue(.main)

    // Built-in JIT (iOS 26+): the app's own helper extension attaches the debugger, using a pairing
    // file made in-app (iOS 27) and kept in the Keychain. No StikDebug needed; LocalDevVPN still is.
    AsyncFunction("builtInJitStatus") { () -> [String: Any] in
      ["available": Gow3BuiltInJIT.unavailableReason == nil, "reason": Gow3BuiltInJIT.unavailableReason ?? "",
       "paired": Gow3BuiltInJIT.loadPairing() != nil, "pairingSupported": Gow3Pairing.isSupported]
    }.runOnQueue(.main)

    AsyncFunction("pairingState") { () -> [String: Any] in
      Gow3Pairing.shared.state
    }.runOnQueue(.main)

    AsyncFunction("startPairing") {
      Gow3Pairing.shared.start()
    }.runOnQueue(.main)

    AsyncFunction("cancelPairing") {
      Gow3Pairing.shared.cancel()
    }.runOnQueue(.main)

    AsyncFunction("forgetPairing") {
      Gow3BuiltInJIT.forgetPairing()
    }.runOnQueue(.main)

    /// Checks that LocalDevVPN reaches the device, and downloads and mounts the Developer Disk Image if
    /// needed. Resolves with the helper's message; rejects with the reason it is not ready.
    AsyncFunction("prepareBuiltInJit") { (promise: Promise) in
      guard let pairing = Gow3BuiltInJIT.loadPairing() else {
        promise.reject(Gow3Error(description: "Pair this iPhone first."))
        return
      }
      Gow3BuiltInJIT.send(Gow3JITRequest(operation: .prepare, targetPID: nil, pairingData: pairing)) { result in
        switch result {
        case .success(let response) where response.success: promise.resolve(response.message)
        case .success(let response): promise.reject(Gow3Error(description: response.message))
        case .failure(let error): promise.reject(Gow3Error(description: error.localizedDescription))
        }
      }
    }.runOnQueue(.main)

    AsyncFunction("resetDeveloperDiskImage") { (promise: Promise) in
      Gow3BuiltInJIT.send(Gow3JITRequest(operation: .resetDDI, targetPID: nil, pairingData: nil)) { result in
        switch result {
        case .success(let response): promise.resolve(response.message)
        case .failure(let error): promise.reject(Gow3Error(description: error.localizedDescription))
        }
      }
    }.runOnQueue(.main)

    /// Starts the helper, which attaches the debugger to this process; then creates a `poolMb` MiB
    /// JIT pool and detaches. Resolves with gow3_jit_setup's result.
    AsyncFunction("enableJitBuiltIn") { (poolMb: Int, promise: Promise) in
      let shot = OneShot()
      let finish: (Int32?) -> Void = { code in
        guard shot.claim() else { return }
        if let code {
          promise.resolve(Int(code))
        } else {
          promise.reject(Gow3Error(description: "The debugger did not attach within 90 s. Is LocalDevVPN connected?"))
        }
      }
      if gow3_jit_debugger_attached() != 0 {
        waitForDebuggerThenSetup(poolMb: poolMb, timeout: 1, shot: OneShot(), finish: finish)
        return
      }
      guard let pairing = Gow3BuiltInJIT.loadPairing() else {
        promise.reject(Gow3Error(description: "Pair this iPhone first."))
        return
      }
      guard let script = Gow3CoreModule.universalScript else {
        promise.reject(Gow3Error(description: "The JIT script is missing from the app."))
        return
      }
      let request = Gow3JITRequest(operation: .enable, targetPID: getpid(), pairingData: pairing,
                                   scriptBase64: script.base64EncodedString())
      Gow3BuiltInJIT.send(request, started: {
        waitForDebuggerThenSetup(poolMb: poolMb, timeout: 90, shot: shot, finish: finish)
      }, completion: { result in
        // The helper answers when the script detaches; that is after a successful setup, or an error.
        let message: String
        switch result {
        case .success(let response) where response.success: return
        case .success(let response): message = response.message
        case .failure(let error): message = error.localizedDescription
        }
        guard shot.claim() else { return }
        promise.reject(Gow3Error(description: message))
      })
    }.runOnQueue(.main)

    /// What the loader needs in Documents/gow3, and which of it is there.
    Function("gameFiles") { () -> [String: Any] in
      let root = Gow3CoreModule.gameRoot
      try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
      func exists(_ name: String) -> Bool { FileManager.default.fileExists(atPath: root.appendingPathComponent(name).path) }
      return ["folder": "Documents/gow3", "boot": exists("boot-linked.bin"), "content": exists("content.bin"),
              "patches": exists("patches.bin"), "eboot": exists("game/eboot.bin"), "sfo": exists("game/sce_sys/param.sfo"),
              "started": Gow3CoreModule.gameStarted]
    }

    /// Starts the game CPU-only: the loader maps the eboot, links it against the runtime and runs it
    /// through FEXCore until the first GPU call, logging to Documents/gow3/loader.log.
    AsyncFunction("startGameCpuOnly") { () -> String in
      try Gow3CoreModule.startLoader(cpuOnly: true)
      return "Loader started; see the game log."
    }

    /// Starts the game with the GPU: the fullscreen Metal view is presented over the app, its layer
    /// becomes the game's window (gow3_gpu_set_surface), then the loader starts. A two-finger double
    /// tap hides the view again (the game keeps running) so the log can be read.
    AsyncFunction("startGame") { (promise: Promise) in
      do {
        try Gow3CoreModule.checkCanStart()
      } catch {
        promise.reject(error)
        return
      }
      Gow3GameViewController.present { controller in
        guard let controller else {
          promise.reject(Gow3Error(description: "No window to present the Metal view in."))
          return
        }
        controller.feedsGame = true
        controller.publishSurface()
        do {
          try Gow3CoreModule.startLoader(cpuOnly: false)
          promise.resolve("Game started; two-finger double tap hides the game view.")
        } catch {
          controller.dismiss(animated: false) { promise.reject(error) }
        }
      }
    }.runOnQueue(.main)

    /// The last 24 KiB of Documents/gow3/loader.log.
    Function("readGameLog") { () -> String in
      guard let data = try? Data(contentsOf: Gow3CoreModule.gameLogURL) else { return "" }
      return String(decoding: data.suffix(24 * 1024), as: UTF8.self)
    }

    /// For a hang: every thread writes where it is to the game log.
    AsyncFunction("dumpGameThreads") { () -> Int in
      Int(gow3_dump_threads())
    }

    Function("clearGameLog") {
      try? FileManager.default.removeItem(at: Gow3CoreModule.gameLogURL)
    }

    /// MoltenVK on the screen: the fullscreen Metal view is cleared to a cycling colour for `seconds`,
    /// then closed. Resolves with the JSON report (device, fps, what shadPS4 would lack).
    AsyncFunction("gpuScreenTest") { (seconds: Int, promise: Promise) in
      Gow3GameViewController.present { controller in
        guard let controller else {
          promise.reject(Gow3Error(description: "No window to present the Metal view in."))
          return
        }
        let layer = controller.metalView.metalLayer
        let size = layer.drawableSize
        let pointer = Unmanaged.passUnretained(layer).toOpaque()
        DispatchQueue.global(qos: .userInteractive).async {
          let json = readJson {
            gow3_gpu_screen_test(pointer, UInt32(size.width), UInt32(size.height), UInt32(max(1, seconds)), $0, $1)
          }
          DispatchQueue.main.async {
            controller.dismiss(animated: false) { promise.resolve(json) }
          }
        }
      }
    }.runOnQueue(.main)

    AsyncFunction("runSelfTest") { () -> String in
      let path = Gow3CoreModule.logURL.path
      return path.withCString { log in readJson { gow3_selftest(log, $0, $1) } }
    }

    // Connected controllers as iOS reports them (the future runtime_pad backend reads the same objects).
    Function("controllers") { () -> [[String: Any]] in
      GCController.controllers().map { c in
        let pad = c.extendedGamepad
        let kind: String
        if pad is GCDualSenseGamepad {
          kind = "DualSense"
        } else if pad is GCDualShockGamepad {
          kind = "DualShock 4"
        } else if pad is GCXboxGamepad {
          kind = "Xbox"
        } else if pad != nil {
          kind = "extended"
        } else {
          kind = "other"
        }
        return [
          "name": c.vendorName ?? "?",
          "category": c.productCategory,
          "kind": kind,
          "haptics": c.haptics != nil,
          "light": c.light != nil,
          "battery": c.battery.map { Double($0.batteryLevel) } ?? -1,
        ]
      }
    }

    Function("readLog") { () -> String in
      (try? String(contentsOf: Gow3CoreModule.logURL, encoding: .utf8)) ?? ""
    }

    Function("clearLog") {
      try? FileManager.default.removeItem(at: Gow3CoreModule.logURL)
    }
  }
}

private extension String {
  func appendLine(to path: String) throws {
    if let handle = FileHandle(forWritingAtPath: path) {
      defer { try? handle.close() }
      try handle.seekToEnd()
      try handle.write(contentsOf: Data(utf8))
    } else {
      try write(toFile: path, atomically: true, encoding: .utf8)
    }
  }
}
