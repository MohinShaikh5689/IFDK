// SPDX-License-Identifier: GPL-3.0-or-later
// The Expo bridge to the native core (native/include/gow3core.h, linked from lib/libgow3core_all.a).
import ExpoModulesCore
import GameController
import UIKit

@_silgen_name("gow3_device_report")
private func gow3_device_report(_ out: UnsafeMutablePointer<CChar>?, _ capacity: Int) -> Int
@_silgen_name("gow3_jit_debugged")
private func gow3_jit_debugged() -> Int32
@_silgen_name("gow3_jit_debugger_attached")
private func gow3_jit_debugger_attached() -> Int32
@_silgen_name("gow3_jit_setup")
private func gow3_jit_setup(_ poolMb: UInt32) -> Int32
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

  public func definition() -> ModuleDefinition {
    Name("Gow3Core")

    Function("deviceReport") { () -> String in
      readJson { gow3_device_report($0, $1) }
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
