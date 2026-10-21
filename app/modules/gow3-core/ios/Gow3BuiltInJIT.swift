// SPDX-License-Identifier: GPL-3.0-or-later
// Built-in JIT: starts the JIT helper extension (PlugIns/Gow3JITHelper.appex) and sends it one
// Gow3JITRequest, after Madeira's JITBuiltInHost.swift. The helper is a classic app extension on
// com.apple.ar.viewer (as LiveContainer's LiveProcess is), started through the private NSExtension
// API by the bundle ID it has in this installation, so it is still found after a sideloader appends
// its team ID to the app's and the helper's bundle IDs.
import Foundation
import Security

@objc private protocol NSExtensionClassShim {
  @objc(extensionWithIdentifier:error:)
  func `extension`(withIdentifier identifier: String) throws -> AnyObject
}

@objc private protocol NSExtensionShim {
  @objc(beginExtensionRequestWithInputItems:completion:)
  func beginExtensionRequest(withInputItems items: [Any], completion: @escaping (NSUUID?) -> Void)
  @objc(setRequestCompletionBlock:)
  func setRequestCompletionBlock(_ block: @escaping (NSUUID?, [Any]?) -> Void)
  @objc(setRequestCancellationBlock:)
  func setRequestCancellationBlock(_ block: @escaping (NSUUID?, NSError?) -> Void)
  @objc(setRequestInterruptionBlock:)
  func setRequestInterruptionBlock(_ block: @escaping (NSUUID?) -> Void)
}

enum Gow3BuiltInJIT {
  static let helperFile = "Gow3JITHelper.appex"

  /// Requests that have started and not finished, kept alive until they do (main thread only).
  private static var running: [HelperRequest] = []

  static var unavailableReason: String? {
    #if targetEnvironment(simulator)
      return "Built-in JIT works only on a physical device."
    #else
      guard #available(iOS 26.0, *) else { return "Built-in JIT needs iOS 26 or later." }
      if getenv("LC_HOME_PATH") != nil { return "Built-in JIT does not work inside LiveContainer." }
      if helperIdentifier == nil {
        return "The JIT helper is missing from this install. Reinstall, and keep app extensions if the sideloader asks."
      }
      return nil
    #endif
  }

  /// The helper's bundle ID in this installation, from its own Info.plist.
  static var helperIdentifier: String? {
    guard let url = Bundle.main.builtInPlugInsURL?.appendingPathComponent(helperFile) else { return nil }
    return Bundle(url: url)?.bundleIdentifier
  }

  /// Sends `request`; `started` runs once the helper process is up, `completion` with its answer.
  /// Call on the main thread; both callbacks run on it.
  static func send(_ request: Gow3JITRequest, started: @escaping () -> Void = {},
                   completion: @escaping (Result<Gow3JITRequest.Response, Error>) -> Void) {
    if let reason = unavailableReason {
      completion(.failure(Gow3BuiltInError(reason)))
      return
    }
    if NSClassFromString("NSExtension") == nil {
      dlopen("/System/Library/Frameworks/ExtensionFoundation.framework/ExtensionFoundation", RTLD_NOW)
    }
    guard let identifier = helperIdentifier, let extensionClass = NSClassFromString("NSExtension") else {
      completion(.failure(Gow3BuiltInError("iOS did not provide the extension API the JIT helper needs.")))
      return
    }
    let factory = unsafeBitCast(extensionClass as AnyObject, to: NSExtensionClassShim.self)
    let found: AnyObject
    do {
      found = try factory.extension(withIdentifier: identifier)
    } catch {
      completion(.failure(Gow3BuiltInError("The JIT helper (\(identifier)) was not found: \(error.localizedDescription) Reinstall the app.")))
      return
    }
    guard let data = try? JSONEncoder().encode(request) else {
      completion(.failure(Gow3BuiltInError("The JIT helper request could not be encoded.")))
      return
    }
    let helper = HelperRequest(extension: found, completion: completion)
    running.append(helper)
    helper.begin(data: data, started: started)
  }

  fileprivate static func finished(_ helper: HelperRequest) {
    running.removeAll { $0 === helper }
  }

  // MARK: Pairing file (a credential for this iPhone: kept in the Keychain, never in Documents,
  // which the Files app can read)

  private static let keychainQuery: [String: Any] = [
    kSecClass as String: kSecClassGenericPassword,
    kSecAttrService as String: "gow3-ios.jit",
    kSecAttrAccount as String: "pairing-file",
  ]

  static func storePairing(_ data: Data) throws {
    SecItemDelete(keychainQuery as CFDictionary)
    var add = keychainQuery
    add[kSecValueData as String] = data
    add[kSecAttrAccessible as String] = kSecAttrAccessibleWhenUnlockedThisDeviceOnly
    let status = SecItemAdd(add as CFDictionary, nil)
    guard status == errSecSuccess else { throw Gow3BuiltInError("The Keychain refused the pairing file (\(status)).") }
  }

  static func loadPairing() -> Data? {
    var query = keychainQuery
    query[kSecReturnData as String] = true
    query[kSecMatchLimit as String] = kSecMatchLimitOne
    var item: CFTypeRef?
    guard SecItemCopyMatching(query as CFDictionary, &item) == errSecSuccess else { return nil }
    return item as? Data
  }

  static func forgetPairing() {
    SecItemDelete(keychainQuery as CFDictionary)
  }
}

struct Gow3BuiltInError: Error, CustomStringConvertible, LocalizedError {
  let description: String
  init(_ description: String) { self.description = description }
  var errorDescription: String? { description }
}

/// One request to the helper, from its start to its one result. Main thread only.
private final class HelperRequest {
  private let shim: NSExtensionShim
  private let found: AnyObject  // the NSExtension, kept alive for the request
  private var completion: ((Result<Gow3JITRequest.Response, Error>) -> Void)?

  init(extension found: AnyObject, completion: @escaping (Result<Gow3JITRequest.Response, Error>) -> Void) {
    self.found = found
    self.shim = unsafeBitCast(found, to: NSExtensionShim.self)
    self.completion = completion
  }

  func begin(data: Data, started: @escaping () -> Void) {
    shim.setRequestCompletionBlock { [weak self] _, items in
      let payload = (items?.first as? NSExtensionItem)?.userInfo?[Gow3JITRequest.Response.itemKey] as? Data
      DispatchQueue.main.async { self?.returned(payload) }
    }
    shim.setRequestCancellationBlock { [weak self] _, error in
      let message = error?.localizedDescription ?? "the request was cancelled"
      DispatchQueue.main.async { self?.end(.failure(Gow3BuiltInError("The JIT helper stopped: \(message)"))) }
    }
    shim.setRequestInterruptionBlock { [weak self] _ in
      DispatchQueue.main.async { self?.end(.failure(Gow3BuiltInError("The JIT helper stopped unexpectedly. Try again."))) }
    }
    let item = NSExtensionItem()
    item.userInfo = [Gow3JITRequest.itemKey: data]
    shim.beginExtensionRequest(withInputItems: [item]) { [weak self] uuid in
      DispatchQueue.main.async {
        guard let self else { return }
        if uuid == nil {
          self.end(.failure(Gow3BuiltInError("The JIT helper did not start. Reinstall, and keep app extensions if the sideloader asks.")))
        } else {
          started()
        }
      }
    }
  }

  private func returned(_ payload: Data?) {
    guard let payload, let response = try? JSONDecoder().decode(Gow3JITRequest.Response.self, from: payload) else {
      end(.failure(Gow3BuiltInError("The JIT helper finished without an answer.")))
      return
    }
    end(.success(response))
  }

  private func end(_ result: Result<Gow3JITRequest.Response, Error>) {
    guard let completion else { return }  // the first result counts
    self.completion = nil
    Gow3BuiltInJIT.finished(self)
    completion(result)
  }
}
