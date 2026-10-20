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
        DispatchQueue.global(qos: .userInitiated).async {
          let deadline = Date().addingTimeInterval(120)
          while gow3_jit_debugger_attached() == 0 {
            if Date() > deadline {
              promise.reject(Gow3Error(description: "StikDebug did not attach within 120 s"))
              return
            }
            Thread.sleep(forTimeInterval: 0.25)
          }
          promise.resolve(Int(gow3_jit_setup(UInt32(poolMb))))
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
