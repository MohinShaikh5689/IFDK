// SPDX-License-Identifier: GPL-3.0-or-later
// On-device pairing for the built-in JIT (iOS 27 and later), after Madeira's JITPairing.swift.
//
// iOS 27 can pair with a computer it finds on the local network, started from the iPhone:
// Settings > Privacy & Security > Developer Mode lists every `_remotepairing-pairable-host._tcp`
// service, and pairing with one asks for the PIN that host shows. The app plays that computer for its
// own iPhone: libgow3_rppairing (native/rppairing, idevice's PairableHost) listens on a port and runs
// the pairing, this file publishes the port through mDNSResponder (dns_sd, so only the Local Network
// permission is needed) and keeps the resulting RPPairing file in the Keychain for the JIT helper.
//
// The user pairs from Settings, so the app must keep running in the background: a
// BGContinuedProcessingTask ("<bundle id>.pairing.session", permitted by
// `$(PRODUCT_BUNDLE_IDENTIFIER).pairing.*` in Info.plist) whose system progress UI also shows the PIN.
// If iOS refuses it, only the ~30 s background grace period remains. The PIN is also posted as a
// local notification.
import BackgroundTasks
import dnssd
import Foundation
import UIKit
import UserNotifications

@_silgen_name("gow3_rppairing_new")
private func gow3_rppairing_new(_ name: UnsafePointer<CChar>, _ error: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> OpaquePointer?
@_silgen_name("gow3_rppairing_port")
private func gow3_rppairing_port(_ session: OpaquePointer) -> UInt16
@_silgen_name("gow3_rppairing_service_name")
private func gow3_rppairing_service_name(_ session: OpaquePointer) -> UnsafePointer<CChar>?
@_silgen_name("gow3_rppairing_txt_count")
private func gow3_rppairing_txt_count(_ session: OpaquePointer) -> Int
@_silgen_name("gow3_rppairing_txt_key")
private func gow3_rppairing_txt_key(_ session: OpaquePointer, _ index: Int) -> UnsafePointer<CChar>?
@_silgen_name("gow3_rppairing_txt_value")
private func gow3_rppairing_txt_value(_ session: OpaquePointer, _ index: Int) -> UnsafePointer<CChar>?
@_silgen_name("gow3_rppairing_accept")
private func gow3_rppairing_accept(
  _ session: OpaquePointer,
  _ pinCallback: (@convention(c) (UnsafePointer<CChar>?, UnsafeMutableRawPointer?) -> Void)?,
  _ pinContext: UnsafeMutableRawPointer?,
  _ outPlist: UnsafeMutablePointer<UnsafeMutablePointer<UInt8>?>?, _ outLen: UnsafeMutablePointer<Int>?,
  _ outDeviceName: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?,
  _ error: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?) -> Int32
@_silgen_name("gow3_rppairing_cancel")
private func gow3_rppairing_cancel(_ session: OpaquePointer)
@_silgen_name("gow3_rppairing_free")
private func gow3_rppairing_free(_ session: OpaquePointer)
@_silgen_name("gow3_rppairing_bytes_free")
private func gow3_rppairing_bytes_free(_ bytes: UnsafeMutablePointer<UInt8>?, _ len: Int)
@_silgen_name("gow3_rppairing_string_free")
private func gow3_rppairing_string_free(_ string: UnsafeMutablePointer<CChar>?)

/// Main thread only.
final class Gow3Pairing {
  static let shared = Gow3Pairing()

  /// Shown on the iPhone as "Pair with gow3-ios"; the host identifier is derived from it.
  static let hostName = "gow3-ios"
  static let timeout: TimeInterval = 300

  enum Phase: String {
    case idle, waiting, pin, paired, failed
  }

  private(set) var phase: Phase = .idle
  private(set) var pin = ""
  private(set) var message = ""
  /// iOS refused the continued-processing task: only ~30 s in the background.
  private(set) var backgroundLimited = false

  static var isSupported: Bool {
    ProcessInfo.processInfo.isOperatingSystemAtLeast(OperatingSystemVersion(majorVersion: 27, minorVersion: 0, patchVersion: 0))
  }

  var active: Bool { phase == .waiting || phase == .pin }

  var state: [String: Any] {
    ["phase": phase.rawValue, "pin": pin, "message": message, "backgroundLimited": backgroundLimited,
     "supported": Gow3Pairing.isSupported, "paired": Gow3BuiltInJIT.loadPairing() != nil]
  }

  private var session: OpaquePointer?
  private var registration: DNSServiceRef?
  private var continued: AnyObject?  // BGContinuedProcessingTask (iOS 26+)
  private var registered: String?
  private var grace: UIBackgroundTaskIdentifier = .invalid
  private var deadline: Timer?

  func start() {
    guard Gow3Pairing.isSupported else {
      fail("In-app pairing needs iOS 27 or later.")
      return
    }
    guard !active, session == nil else { return }
    var error: UnsafeMutablePointer<CChar>?
    guard let session = gow3_rppairing_new(Gow3Pairing.hostName, &error) else {
      fail(Gow3Pairing.take(error) ?? "Could not open the pairing port.")
      return
    }
    self.session = session
    phase = .waiting
    pin = ""
    message = "Open Settings > Privacy & Security > Developer Mode, scroll down and tap Pair with \(Gow3Pairing.hostName). Keep Wi-Fi on."
    backgroundLimited = false
    UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound]) { _, _ in }
    guard publish(session) else {
      gow3_rppairing_free(session)
      self.session = nil
      return
    }
    beginBackground()
    deadline = Timer.scheduledTimer(withTimeInterval: Gow3Pairing.timeout, repeats: false) { _ in
      Gow3Pairing.shared.cancel(reason: "Pairing timed out. Try again when you are ready to pair from Settings.")
    }
    DispatchQueue.global(qos: .userInitiated).async {
      var bytes: UnsafeMutablePointer<UInt8>?
      var length = 0
      var device: UnsafeMutablePointer<CChar>?
      var failure: UnsafeMutablePointer<CChar>?
      let status = gow3_rppairing_accept(session, { pin, _ in
        guard let pin else { return }
        let code = String(cString: pin)
        DispatchQueue.main.async { Gow3Pairing.shared.showPin(code) }
      }, nil, &bytes, &length, &device, &failure)
      let data = bytes.map { Data(bytes: $0, count: length) }
      gow3_rppairing_bytes_free(bytes, length)
      let name = Gow3Pairing.take(device)
      let message = Gow3Pairing.take(failure)
      DispatchQueue.main.async { Gow3Pairing.shared.finished(status: status, data: data, device: name, message: message) }
    }
  }

  /// Stops a pairing in progress; `reason` is shown as the failure, nil returns to idle.
  func cancel(reason: String? = nil) {
    guard let session else { return }
    gow3_rppairing_cancel(session)
    stopAdvertising()
    if let reason {
      phase = .failed
      message = reason
    } else {
      phase = .idle
      message = ""
    }
  }

  private func showPin(_ code: String) {
    guard active else { return }
    phase = .pin
    pin = code
    message = "Enter this code on your iPhone."
    if #available(iOS 26.0, *), let task = continued as? BGContinuedProcessingTask {
      task.updateTitle("Pairing code \(code)", subtitle: "Enter it on this iPhone to pair with \(Gow3Pairing.hostName)")
      task.progress.completedUnitCount = 1
    }
    if UIApplication.shared.applicationState != .active {
      notify("\(Gow3Pairing.hostName) pairing code: \(code)", body: "Enter this code on your iPhone to finish pairing.")
    }
  }

  private func finished(status: Int32, data: Data?, device: String?, message failure: String?) {
    if let session { gow3_rppairing_free(session) }
    session = nil
    stopAdvertising()
    deadline?.invalidate()
    deadline = nil
    switch status {
    case 0:
      do {
        guard let data else { throw Gow3BuiltInError("no pairing data") }
        try Gow3BuiltInJIT.storePairing(data)
        phase = .paired
        pin = ""
        message = "Paired with \(device ?? "this iPhone")."
        if UIApplication.shared.applicationState != .active {
          notify("Paired with \(Gow3Pairing.hostName)", body: "Return to the app to enable JIT.")
        }
        endBackground(success: true)
      } catch {
        fail("Pairing finished, but the pairing file could not be saved.")
      }
    case 2:
      endBackground(success: false)
    default:
      fail(failure.map { "Pairing failed: \($0)" } ?? "Pairing failed. Try again.")
    }
  }

  private func fail(_ text: String) {
    if let session { gow3_rppairing_cancel(session) }
    stopAdvertising()
    phase = .failed
    pin = ""
    message = text
    endBackground(success: false)
  }

  private static func take(_ string: UnsafeMutablePointer<CChar>?) -> String? {
    guard let string else { return nil }
    defer { gow3_rppairing_string_free(string) }
    return String(cString: string)
  }

  // MARK: Advertising

  private func publish(_ session: OpaquePointer) -> Bool {
    var txt = TXTRecordRef()
    TXTRecordCreate(&txt, 0, nil)
    defer { TXTRecordDeallocate(&txt) }
    for index in 0..<gow3_rppairing_txt_count(session) {
      guard let key = gow3_rppairing_txt_key(session, index), let value = gow3_rppairing_txt_value(session, index) else { continue }
      TXTRecordSetValue(&txt, key, UInt8(clamping: strlen(value)), value)
    }
    var ref: DNSServiceRef?
    let error = DNSServiceRegister(
      &ref, 0, 0, gow3_rppairing_service_name(session), "_remotepairing-pairable-host._tcp", nil, nil,
      gow3_rppairing_port(session).bigEndian, TXTRecordGetLength(&txt), TXTRecordGetBytesPtr(&txt),
      { _, _, error, _, _, _, _ in
        guard error != DNSServiceErrorType(kDNSServiceErr_NoError) else { return }
        DispatchQueue.main.async { Gow3Pairing.shared.advertisingFailed(error) }
      }, nil)
    guard error == DNSServiceErrorType(kDNSServiceErr_NoError), let ref else {
      advertisingFailed(error)
      return false
    }
    DNSServiceSetDispatchQueue(ref, .main)
    registration = ref
    return true
  }

  private func advertisingFailed(_ error: DNSServiceErrorType) {
    guard active || session != nil else { return }
    fail(error == DNSServiceErrorType(kDNSServiceErr_PolicyDenied)
      ? "Local Network access is needed to pair. Allow it in Settings > Apps > \(Gow3Pairing.hostName), then try again."
      : "Could not announce the app on the local network (\(error)). Check that Wi-Fi is on, then try again.")
  }

  private func stopAdvertising() {
    if let registration { DNSServiceRefDeallocate(registration) }
    registration = nil
  }

  // MARK: Background

  /// The permitted "<bundle id>.pairing.*" identifier from Info.plist, made concrete.
  private var taskIdentifier: String? {
    let permitted = Bundle.main.object(forInfoDictionaryKey: "BGTaskSchedulerPermittedIdentifiers") as? [String] ?? []
    guard let wildcard = permitted.first(where: { $0.hasSuffix(".pairing.*") }) else { return nil }
    return String(wildcard.dropLast()) + "session"
  }

  private func beginBackground() {
    grace = UIApplication.shared.beginBackgroundTask(withName: "gow3 pairing") {
      let pairing = Gow3Pairing.shared
      if pairing.continued == nil && pairing.active {
        pairing.cancel(reason: "iOS stopped the app in the background before pairing finished. Try again, and pair from Settings straight away.")
      }
      pairing.endGrace()
    }
    if #available(iOS 26.0, *) { submitContinued() } else { backgroundLimited = true }
  }

  @available(iOS 26.0, *)
  private func submitContinued() {
    guard let identifier = taskIdentifier else {
      backgroundLimited = true
      return
    }
    if registered != identifier {
      let ok = BGTaskScheduler.shared.register(forTaskWithIdentifier: identifier, using: .main) { task in
        guard let task = task as? BGContinuedProcessingTask else {
          task.setTaskCompleted(success: false)
          return
        }
        Gow3Pairing.shared.attach(task)
      }
      guard ok else {
        backgroundLimited = true
        return
      }
      registered = identifier
    }
    let request = BGContinuedProcessingTaskRequest(identifier: identifier, title: "Pairing with \(Gow3Pairing.hostName)",
                                                   subtitle: "Settings > Privacy & Security > Developer Mode")
    request.strategy = .fail
    do {
      try BGTaskScheduler.shared.submit(request)
    } catch {
      backgroundLimited = true
    }
  }

  @available(iOS 26.0, *)
  private func attach(_ task: BGContinuedProcessingTask) {
    guard active else {
      task.setTaskCompleted(success: false)
      return
    }
    continued = task
    task.progress.totalUnitCount = 2
    if phase == .pin {
      task.updateTitle("Pairing code \(pin)", subtitle: "Enter it on this iPhone to pair with \(Gow3Pairing.hostName)")
      task.progress.completedUnitCount = 1
    }
    task.expirationHandler = {
      DispatchQueue.main.async {
        Gow3Pairing.shared.continued = nil
        Gow3Pairing.shared.cancel(reason: "iOS stopped the pairing in the background. Try again.")
      }
    }
  }

  private func endBackground(success: Bool) {
    if #available(iOS 26.0, *), let task = continued as? BGContinuedProcessingTask {
      task.progress.completedUnitCount = task.progress.totalUnitCount
      task.setTaskCompleted(success: success)
    }
    continued = nil
    endGrace()
  }

  private func endGrace() {
    guard grace != .invalid else { return }
    UIApplication.shared.endBackgroundTask(grace)
    grace = .invalid
  }

  private func notify(_ title: String, body: String) {
    let content = UNMutableNotificationContent()
    content.title = title
    content.body = body
    content.sound = .default
    UNUserNotificationCenter.current().add(UNNotificationRequest(identifier: "gow3.pairing.\(UUID().uuidString)", content: content, trigger: nil))
  }
}
