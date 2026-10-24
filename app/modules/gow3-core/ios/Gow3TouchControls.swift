// SPDX-License-Identifier: GPL-3.0-or-later
// On-screen controls over the game view: two sticks, the face buttons, the d-pad, L1/L2/R1/R2,
// L3/R3, Options and the touchpad. Their state goes to the pad code (runtime_pad.c, gow3_touch_pad)
// and adds to a connected controller's. The launcher's switch decides whether they show; they hide
// while a controller is connected, and the small button at the top shows or hides them in play.
// A long press on that button edits the layout: drag a control, -/+ resize it, Reset, Done.
import GameController
import UIKit

@_silgen_name("gow3_touch_pad")
private func gow3_touch_pad(_ buttons: UInt32, _ sticks: UInt32)

/// libScePad button bits (runtime_pad.c).
private enum Pad {
  static let l3: UInt32 = 0x2, r3: UInt32 = 0x4, options: UInt32 = 0x8
  static let up: UInt32 = 0x10, right: UInt32 = 0x20, down: UInt32 = 0x40, left: UInt32 = 0x80
  static let l2: UInt32 = 0x100, r2: UInt32 = 0x200, l1: UInt32 = 0x400, r1: UInt32 = 0x800
  static let triangle: UInt32 = 0x1000, circle: UInt32 = 0x2000, cross: UInt32 = 0x4000, square: UInt32 = 0x8000
  /// A click on the right side of the touchpad (the pad code plays it as a touch and a click).
  static let touchpad: UInt32 = 0x8000_0000
  static let centered: UInt32 = 0x8080_8080
}

enum Gow3TouchControls {
  private static let key = "gow3.touchControls"
  private static let layoutKey = "gow3.touchLayout"
  /// The launcher's switch: on until it is turned off.
  static var enabled: Bool {
    get { UserDefaults.standard.object(forKey: key) as? Bool ?? true }
    set { UserDefaults.standard.set(newValue, forKey: key) }
  }
  /// Per control group: [x offset, y offset] as fractions of the screen, and a scale.
  static var layout: [String: [Double]] {
    get { UserDefaults.standard.dictionary(forKey: layoutKey) as? [String: [Double]] ?? [:] }
    set { UserDefaults.standard.set(newValue, forKey: layoutKey) }
  }
}

private final class PadButton: UIView {
  let bits: UInt32
  private let label = UILabel()
  private let rest = UIColor(white: 0.1, alpha: 0.45)
  private let held = UIColor(white: 0.9, alpha: 0.6)

  var pressed = false {
    didSet { if pressed != oldValue { backgroundColor = pressed ? held : rest } }
  }

  init(_ title: String, _ bits: UInt32, color: UIColor = .white, size: CGFloat = 22) {
    self.bits = bits
    super.init(frame: .zero)
    isUserInteractionEnabled = false
    backgroundColor = rest
    layer.borderWidth = 1.5
    layer.borderColor = UIColor(white: 1, alpha: 0.35).cgColor
    label.text = title
    label.textColor = color
    label.font = .systemFont(ofSize: size, weight: .semibold)
    label.textAlignment = .center
    addSubview(label)
  }

  required init?(coder: NSCoder) { fatalError() }

  override func layoutSubviews() {
    super.layoutSubviews()
    label.frame = bounds
    layer.cornerRadius = min(bounds.width, bounds.height) / 2
  }
}

private final class PadStick: UIView {
  private let knob = UIView()
  /// -1...1 on both axes, y down.
  private(set) var value = CGPoint.zero

  override init(frame: CGRect) {
    super.init(frame: frame)
    isUserInteractionEnabled = false
    backgroundColor = UIColor(white: 0.1, alpha: 0.3)
    layer.borderWidth = 1.5
    layer.borderColor = UIColor(white: 1, alpha: 0.3).cgColor
    knob.backgroundColor = UIColor(white: 0.85, alpha: 0.5)
    addSubview(knob)
  }

  required init?(coder: NSCoder) { fatalError() }

  override func layoutSubviews() {
    super.layoutSubviews()
    layer.cornerRadius = bounds.width / 2
    let size = bounds.width * 0.45
    knob.bounds = CGRect(x: 0, y: 0, width: size, height: size)
    knob.layer.cornerRadius = size / 2
    place()
  }

  /// Follows a finger at `point` (in this view), clamped to the rim.
  func track(_ point: CGPoint) {
    let radius = bounds.width / 2
    var dx = point.x - bounds.midX, dy = point.y - bounds.midY
    let length = hypot(dx, dy)
    if length > radius {
      dx *= radius / length
      dy *= radius / length
    }
    value = CGPoint(x: dx / radius, y: dy / radius)
    place()
  }

  func release() {
    value = .zero
    place()
  }

  private func place() {
    let radius = bounds.width / 2
    knob.center = CGPoint(x: bounds.midX + value.x * radius, y: bounds.midY + value.y * radius)
  }

  /// The pad's two bytes for this stick (128 centered, 0 left/up).
  var bytes: (UInt32, UInt32) {
    func byte(_ v: CGFloat) -> UInt32 { UInt32(max(0, min(255, (128 + v * 127.5).rounded()))) }
    return (byte(value.x), byte(value.y))
  }
}

final class Gow3TouchControlsView: UIView {
  private enum Owner {
    case stick(PadStick)
    case button(PadButton?)
  }

  private let leftStick = PadStick(), rightStick = PadStick()
  private let cross = PadButton("✕", Pad.cross, color: UIColor(red: 0.55, green: 0.7, blue: 1, alpha: 1))
  private let circle = PadButton("○", Pad.circle, color: UIColor(red: 1, green: 0.45, blue: 0.5, alpha: 1))
  private let square = PadButton("□", Pad.square, color: UIColor(red: 1, green: 0.55, blue: 0.85, alpha: 1))
  private let triangle = PadButton("△", Pad.triangle, color: UIColor(red: 0.4, green: 0.9, blue: 0.75, alpha: 1))
  private let up = PadButton("▲", Pad.up, size: 16), down = PadButton("▼", Pad.down, size: 16)
  private let left = PadButton("◀", Pad.left, size: 16), right = PadButton("▶", Pad.right, size: 16)
  private let l1 = PadButton("L1", Pad.l1, size: 16), l2 = PadButton("L2", Pad.l2, size: 16)
  private let r1 = PadButton("R1", Pad.r1, size: 16), r2 = PadButton("R2", Pad.r2, size: 16)
  private let l3 = PadButton("L3", Pad.l3, size: 13), r3 = PadButton("R3", Pad.r3, size: 13)
  private let options = PadButton("Options", Pad.options, size: 13)
  private let touchpad = PadButton("Touchpad", Pad.touchpad, size: 13)
  private lazy var buttons = [cross, circle, square, triangle, up, down, left, right, l1, l2, r1, r2, l3, r3, options, touchpad]
  private let toggle = UIButton(type: .system)
  private let reset = UIButton(type: .system), smaller = UIButton(type: .system), larger = UIButton(type: .system)
  /// The controls that move and resize together, by the name their layout is saved under.
  private lazy var groups: [(String, [UIView])] = [
    ("leftStick", [leftStick]), ("rightStick", [rightStick]), ("face", [cross, circle, square, triangle]),
    ("dpad", [up, down, left, right]), ("l1", [l1]), ("l2", [l2]), ("r1", [r1]), ("r2", [r2]),
    ("l3", [l3]), ("r3", [r3]), ("options", [options]), ("touchpad", [touchpad]),
  ]
  private var layout = Gow3TouchControls.layout
  private var editing = false
  private var selected: String?
  private var drag: (touch: ObjectIdentifier, start: CGPoint, offset: [Double])?
  private var owners: [ObjectIdentifier: Owner] = [:]
  private var sent: (UInt32, UInt32) = (0, Pad.centered)

  /// Whether the controls show (the toggle button always does).
  var shown = true {
    didSet { applyShown() }
  }

  private func applyShown() {
    let visible = shown || editing
    for control in (buttons as [UIView]) + [leftStick, rightStick] { control.isHidden = !visible }
    for button in [reset, smaller, larger] { button.isHidden = !editing }
    toggle.setTitle(editing ? "Done" : shown ? "Hide" : "Pad", for: .normal)
    toggle.alpha = visible ? 0.8 : 0.4
    for (name, members) in groups {
      for member in members {
        member.layer.borderColor = UIColor(white: 1, alpha: 0.35).cgColor
        if editing { member.layer.borderColor = (name == selected ? UIColor.systemYellow : UIColor.systemTeal).cgColor }
      }
    }
    if !shown || editing { releaseAll() }
  }

  override init(frame: CGRect) {
    super.init(frame: frame)
    isMultipleTouchEnabled = true
    backgroundColor = .clear
    for control in ([leftStick, rightStick] as [UIView]) + buttons { addSubview(control) }
    toggle.titleLabel?.font = .systemFont(ofSize: 13, weight: .semibold)
    toggle.setTitleColor(.white, for: .normal)
    toggle.backgroundColor = UIColor(white: 0.1, alpha: 0.45)
    toggle.layer.cornerRadius = 8
    toggle.addTarget(self, action: #selector(flip), for: .touchUpInside)
    toggle.addGestureRecognizer(UILongPressGestureRecognizer(target: self, action: #selector(edit(_:))))
    addSubview(toggle)
    for (button, title, action) in [(reset, "Reset", #selector(resetLayout)), (smaller, "−", #selector(shrink)),
                                    (larger, "+", #selector(grow))] {
      button.setTitle(title, for: .normal)
      button.titleLabel?.font = .systemFont(ofSize: 15, weight: .bold)
      button.setTitleColor(.white, for: .normal)
      button.backgroundColor = UIColor(white: 0.1, alpha: 0.6)
      button.layer.cornerRadius = 8
      button.addTarget(self, action: action, for: .touchUpInside)
      addSubview(button)
    }
    // Observers do not run for assignments in the initializer.
    shown = Gow3TouchControls.enabled && GCController.controllers().isEmpty
    applyShown()
    let center = NotificationCenter.default
    center.addObserver(self, selector: #selector(controllersChanged), name: .GCControllerDidConnect, object: nil)
    center.addObserver(self, selector: #selector(controllersChanged), name: .GCControllerDidDisconnect, object: nil)
  }

  required init?(coder: NSCoder) { fatalError() }

  @objc private func flip() {
    if editing {
      editing = false
      selected = nil
      Gow3TouchControls.layout = layout
      applyShown()
    } else {
      shown.toggle()
    }
  }

  @objc private func edit(_ press: UILongPressGestureRecognizer) {
    guard press.state == .began, !editing else { return }
    editing = true
    selected = nil
    applyShown()
  }

  @objc private func resetLayout() {
    layout = [:]
    selected = nil
    applyShown()
    setNeedsLayout()
  }

  @objc private func shrink() { scaleSelected(by: -0.1) }
  @objc private func grow() { scaleSelected(by: 0.1) }

  private func scaleSelected(by step: Double) {
    guard let name = selected else { return }
    var entry = layout[name] ?? [0, 0, 1]
    entry[2] = max(0.5, min(2.0, entry[2] + step))
    layout[name] = entry
    setNeedsLayout()
  }

  /// A controller hides the controls; without one they come back (if the launcher's switch is on).
  @objc private func controllersChanged() {
    shown = Gow3TouchControls.enabled && GCController.controllers().isEmpty
  }

  override func willMove(toWindow newWindow: UIWindow?) {
    super.willMove(toWindow: newWindow)
    if newWindow == nil { releaseAll() }
  }

  override func layoutSubviews() {
    super.layoutSubviews()
    let w = bounds.width, h = bounds.height
    let inset = safeAreaInsets
    let l = max(inset.left, 16), r = max(inset.right, 16), t = max(inset.top, 10), b = max(inset.bottom, 12)
    let s = min(h / 400, 1.25)

    let stick = 130 * s
    leftStick.frame = CGRect(x: l + 24 * s, y: h - b - stick - 16 * s, width: stick, height: stick)
    l3.frame = CGRect(x: leftStick.frame.maxX - 18 * s, y: leftStick.frame.minY - 30 * s, width: 40 * s, height: 40 * s)

    let face = 58 * s, gap = 62 * s
    let c = CGPoint(x: w - r - 24 * s - gap - face / 2, y: h - b - 16 * s - gap - face / 2)
    for (button, dx, dy) in [(triangle, 0, -1), (cross, 0, 1), (square, -1, 0), (circle, 1, 0)] as [(PadButton, CGFloat, CGFloat)] {
      button.frame = CGRect(x: c.x + dx * gap - face / 2, y: c.y + dy * gap - face / 2, width: face, height: face)
    }

    let small = 100 * s
    rightStick.frame = CGRect(x: square.frame.minX - 40 * s - small, y: h - b - small - 8 * s, width: small, height: small)
    r3.frame = CGRect(x: rightStick.frame.midX - 20 * s, y: rightStick.frame.minY - 46 * s, width: 40 * s, height: 40 * s)

    let arrow = 44 * s, step = 46 * s
    let d = CGPoint(x: leftStick.frame.maxX + 40 * s + step, y: h - b - 16 * s - step - arrow / 2)
    for (button, dx, dy) in [(up, 0, -1), (down, 0, 1), (left, -1, 0), (right, 1, 0)] as [(PadButton, CGFloat, CGFloat)] {
      button.frame = CGRect(x: d.x + dx * step - arrow / 2, y: d.y + dy * step - arrow / 2, width: arrow, height: arrow)
    }

    let shoulder = CGSize(width: 84 * s, height: 40 * s)
    l2.frame = CGRect(origin: CGPoint(x: l + 8 * s, y: t), size: shoulder)
    l1.frame = CGRect(origin: CGPoint(x: l + 8 * s, y: t + 50 * s), size: shoulder)
    r2.frame = CGRect(origin: CGPoint(x: w - r - 8 * s - shoulder.width, y: t), size: shoulder)
    r1.frame = CGRect(origin: CGPoint(x: w - r - 8 * s - shoulder.width, y: t + 50 * s), size: shoulder)

    let pill = CGSize(width: 84 * s, height: 32 * s)
    touchpad.frame = CGRect(origin: CGPoint(x: w / 2 - 40 * s - pill.width, y: t), size: pill)
    options.frame = CGRect(origin: CGPoint(x: w / 2 + 40 * s, y: t), size: pill)
    toggle.frame = CGRect(x: w / 2 - 28 * s, y: t, width: 56 * s, height: 32 * s)
    let tool = CGSize(width: 56 * s, height: 32 * s)
    smaller.frame = CGRect(origin: CGPoint(x: w / 2 - 28 * s - 8 - tool.width, y: t + 40 * s), size: tool)
    larger.frame = CGRect(origin: CGPoint(x: w / 2 + 28 * s + 8, y: t + 40 * s), size: tool)
    reset.frame = CGRect(origin: CGPoint(x: w / 2 - tool.width / 2, y: t + 40 * s), size: tool)

    // The saved layout: each group scaled about its center, then moved.
    for (name, members) in groups {
      guard let entry = layout[name], entry.count == 3 else { continue }
      let box = members.map(\.frame).reduce(members[0].frame) { $0.union($1) }
      let center = CGPoint(x: box.midX, y: box.midY), scale = CGFloat(entry[2])
      let shift = CGPoint(x: CGFloat(entry[0]) * w, y: CGFloat(entry[1]) * h)
      for member in members {
        let f = member.frame
        member.frame = CGRect(x: center.x + (f.minX - center.x) * scale + shift.x,
                              y: center.y + (f.minY - center.y) * scale + shift.y,
                              width: f.width * scale, height: f.height * scale)
      }
    }
  }

  /// Touches belong to this view only on a control (or near a stick), so the rest of the screen
  /// still reaches the game view's gestures.
  override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
    if toggle.frame.insetBy(dx: -8, dy: -8).contains(point) || editing { return true }
    guard shown else { return false }
    return stickNear(point) != nil || button(at: point) != nil
  }

  override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
    guard self.point(inside: point, with: event) else { return nil }
    if toggle.frame.insetBy(dx: -8, dy: -8).contains(point) { return toggle }
    if editing, let tool = [reset, smaller, larger].first(where: { $0.frame.contains(point) }) { return tool }
    return self
  }

  private func group(at point: CGPoint) -> String? {
    groups.first { $0.1.contains { $0.frame.insetBy(dx: -6, dy: -6).contains(point) } }?.0
  }

  private func stickNear(_ point: CGPoint) -> PadStick? {
    for stick in [leftStick, rightStick] where stick.frame.insetBy(dx: -30, dy: -30).contains(point) {
      return stick
    }
    return nil
  }

  private func button(at point: CGPoint) -> PadButton? {
    buttons.first { $0.frame.insetBy(dx: -6, dy: -6).contains(point) }
  }

  override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
    if editing {
      guard drag == nil, let touch = touches.first else { return }
      let point = touch.location(in: self)
      selected = group(at: point)
      if let name = selected { drag = (ObjectIdentifier(touch), point, layout[name] ?? [0, 0, 1]) }
      applyShown()
      return
    }
    for touch in touches {
      let point = touch.location(in: self)
      if let stick = stickNear(point), !owners.values.contains(where: { if case .stick(let s) = $0 { return s === stick } else { return false } }) {
        owners[ObjectIdentifier(touch)] = .stick(stick)
        stick.track(touch.location(in: stick))
      } else {
        owners[ObjectIdentifier(touch)] = .button(button(at: point))
      }
    }
    publish()
  }

  override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
    if editing {
      guard let drag, let name = selected, let touch = touches.first(where: { ObjectIdentifier($0) == drag.touch }),
            bounds.width > 0, bounds.height > 0 else { return }
      let point = touch.location(in: self)
      layout[name] = [drag.offset[0] + Double((point.x - drag.start.x) / bounds.width),
                      drag.offset[1] + Double((point.y - drag.start.y) / bounds.height), drag.offset[2]]
      setNeedsLayout()
      return
    }
    for touch in touches {
      switch owners[ObjectIdentifier(touch)] {
      case .stick(let stick):
        stick.track(touch.location(in: stick))
      case .button:
        // A thumb can slide from one button to the next.
        owners[ObjectIdentifier(touch)] = .button(button(at: touch.location(in: self)))
      case nil:
        break
      }
    }
    publish()
  }

  override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
    if let drag, touches.contains(where: { ObjectIdentifier($0) == drag.touch }) { self.drag = nil }
    if editing { return }
    for touch in touches {
      if case .stick(let stick) = owners.removeValue(forKey: ObjectIdentifier(touch)) { stick.release() }
    }
    publish()
  }

  override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
    touchesEnded(touches, with: event)
  }

  private func releaseAll() {
    owners.removeAll()
    leftStick.release()
    rightStick.release()
    publish()
  }

  private func publish() {
    var bits: UInt32 = 0
    for case .button(let button?) in owners.values { bits |= button.bits }
    for button in buttons { button.pressed = bits & button.bits != 0 }
    let (lx, ly) = leftStick.bytes, (rx, ry) = rightStick.bytes
    let sticks = lx | ly << 8 | rx << 16 | ry << 24
    if sent != (bits, sticks) {
      sent = (bits, sticks)
      gow3_touch_pad(bits, sticks)
    }
  }
}
