// SPDX-License-Identifier: GPL-3.0-or-later
// Messages between the app and its JIT helper extension (native/jit-helper), after Madeira's
// JITBuiltInMessages.swift. Each travels as JSON in an extension request item's userInfo.
// The helper build compiles this same file.
import Foundation

struct Gow3JITRequest: Codable {
  static let itemKey = "gow3-jit-request"

  enum Operation: String, Codable {
    /// Checks that LocalDevVPN reaches the device and that the Developer Disk Image is mounted
    /// (downloading and mounting it if needed).
    case prepare
    /// Attaches the debugger to `targetPID` and runs the JIT script until it detaches.
    case enable
    case resetDDI
  }

  let operation: Operation
  let targetPID: Int32?
  let pairingData: Data?
  /// The JIT script for `enable` (the app's bundled copy of StikJIT's universal script).
  var scriptBase64: String? = nil

  struct Response: Codable {
    static let itemKey = "gow3-jit-response"
    let success: Bool
    let message: String
    let txmPresent: Bool?
  }
}
