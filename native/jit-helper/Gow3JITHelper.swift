// SPDX-License-Identifier: GPL-3.0-or-later
// The JIT helper: a classic app extension (PlugIns/Gow3JITHelper.appex) that debugs the app on its
// behalf, since a process cannot debug itself. After Madeira's MadeiraJITHelper.swift. The app starts
// it by bundle ID (Gow3BuiltInJIT.swift); each extension request carries one Gow3JITRequest, and the
// request completes with the response when the work is done. For `enable` that is when the JIT
// script has detached, so this process lives as long as the debugger does.
import Foundation
import StikJIT

private enum HelperWork {
  /// One request at a time.
  static let queue = DispatchQueue(label: "gow3.jit-helper")

  static func fail(_ code: Int, _ message: String) -> NSError {
    NSError(domain: "Gow3JITHelper", code: code, userInfo: [NSLocalizedDescriptionKey: message])
  }

  static func handle(_ request: Gow3JITRequest) -> Gow3JITRequest.Response {
    let manager = FileManager.default
    let root = manager.urls(for: .libraryDirectory, in: .userDomainMask)[0]
      .appendingPathComponent("StikJIT", isDirectory: true)
    let paths = DDIPaths.default(in: root)
    do {
      try manager.createDirectory(at: root, withIntermediateDirectories: true)
      if request.operation == .resetDDI {
        try StikJIT.resetCachedDDI(at: paths)
        return .init(success: true, message: "Developer Disk Image cache reset.", txmPresent: StikJIT.isTXMPresent)
      }
      guard let pairingData = request.pairingData, !pairingData.isEmpty else {
        throw fail(1, "The pairing file was not provided.")
      }
      let pairingURL = root.appendingPathComponent("pairingFile-\(UUID().uuidString).plist")
      try pairingData.write(to: pairingURL, options: .atomic)
      defer { try? manager.removeItem(at: pairingURL) }

      switch request.operation {
      case .prepare:
        switch StikJIT.prepareDevice(pairingFile: pairingURL, paths: paths,
                                     progress: { NSLog("[gow3-jit] prepare: %@", String(describing: $0)) }) {
        case .ready(let security):
          return .init(success: true, message: "LocalDevVPN reaches the device and the Developer Disk Image is ready.",
                       txmPresent: security.isTXMPresent)
        case .unreachable(let reason), .preparationFailed(let reason):
          return .init(success: false, message: reason, txmPresent: StikJIT.isTXMPresent)
        @unknown default:
          return .init(success: false, message: "StikJIT returned an unknown preparation state.", txmPresent: StikJIT.isTXMPresent)
        }
      case .enable:
        guard let pid = request.targetPID, let script = request.scriptBase64, !script.isEmpty else {
          throw fail(2, "The target process or the JIT script was not provided.")
        }
        try StikJIT.enableJIT(targetPID: pid, pairingFile: pairingURL, ddiPaths: paths,
                              script: .customBase64(script), forceScript: true,
                              preparationProgress: { NSLog("[gow3-jit] prepare: %@", String(describing: $0)) },
                              progress: { NSLog("[gow3-jit] %@", $0) })
        return .init(success: true, message: "The JIT script detached cleanly.", txmPresent: StikJIT.isTXMPresent)
      case .resetDDI:
        preconditionFailure("handled above")
      }
    } catch {
      return .init(success: false, message: error.localizedDescription, txmPresent: StikJIT.isTXMPresent)
    }
  }
}

/// NSExtensionPrincipalClass in Info.plist.
@objc(Gow3JITHelperHandler)
final class Gow3JITHelperHandler: NSObject, NSExtensionRequestHandling {
  func beginRequest(with context: NSExtensionContext) {
    let data = (context.inputItems.first as? NSExtensionItem)?.userInfo?[Gow3JITRequest.itemKey] as? Data
    HelperWork.queue.async {
      let response: Gow3JITRequest.Response
      if let data, let request = try? JSONDecoder().decode(Gow3JITRequest.self, from: data) {
        response = HelperWork.handle(request)
      } else {
        response = .init(success: false, message: "The JIT helper received no request.", txmPresent: nil)
      }
      let item = NSExtensionItem()
      item.userInfo = [Gow3JITRequest.Response.itemKey: (try? JSONEncoder().encode(response)) ?? Data()]
      context.completeRequest(returningItems: [item], completionHandler: nil)
    }
  }
}
