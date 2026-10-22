// SPDX-License-Identifier: GPL-3.0-or-later
// The native fullscreen view the GPU draws into: a CAMetalLayer-backed view in a view controller
// presented over React Native, which stays idle behind it. The MoltenVK screen test and the game's
// Vulkan presenter (gow3_gpu_set_surface) draw into it.
import QuartzCore
import UIKit

final class Gow3MetalView: UIView {
  override class var layerClass: AnyClass { CAMetalLayer.self }
  var metalLayer: CAMetalLayer { layer as! CAMetalLayer }
}

final class Gow3GameViewController: UIViewController {
  let metalView = Gow3MetalView()

  override func loadView() {
    metalView.backgroundColor = .black
    view = metalView
  }

  /// The game presents into this view's layer: its size goes to the GPU library on every layout.
  var feedsGame = false

  override func viewDidLoad() {
    super.viewDidLoad()
    let hide = UITapGestureRecognizer(target: self, action: #selector(hideGameView))
    hide.numberOfTouchesRequired = 2
    hide.numberOfTapsRequired = 2
    view.addGestureRecognizer(hide)
  }

  @objc private func hideGameView() {
    guard feedsGame else { return }
    dismiss(animated: false)
  }

  /// Hands the layer, its pixel size and the screen's maximum refresh rate to the GPU library.
  func publishSurface() {
    let layer = metalView.metalLayer
    let size = layer.drawableSize
    let hz = view.window?.screen.maximumFramesPerSecond ?? UIScreen.main.maximumFramesPerSecond
    gow3_gpu_set_surface(Unmanaged.passUnretained(layer).toOpaque(), UInt32(size.width), UInt32(size.height), UInt32(hz))
  }

  override var prefersStatusBarHidden: Bool { true }
  override var prefersHomeIndicatorAutoHidden: Bool { true }
  // The game is 16:9: landscape only while this view is up (app.json allows every orientation).
  override var supportedInterfaceOrientations: UIInterfaceOrientationMask { .landscape }
  override var preferredInterfaceOrientationForPresentation: UIInterfaceOrientation { .landscapeRight }

  // The drawable follows the view in pixels (MoltenVK sizes the swapchain from it).
  override func viewDidLayoutSubviews() {
    super.viewDidLayoutSubviews()
    let scale = view.window?.screen.nativeScale ?? UIScreen.main.nativeScale
    metalView.metalLayer.contentsScale = scale
    metalView.metalLayer.drawableSize = CGSize(width: view.bounds.width * scale, height: view.bounds.height * scale)
    if feedsGame { publishSurface() }
  }

  /// Presents a new one fullscreen over the topmost view controller; nil when there is no key window.
  static func present(completion: @escaping (Gow3GameViewController?) -> Void) {
    let windows = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows)
    guard var top = windows.first(where: \.isKeyWindow)?.rootViewController else {
      completion(nil)
      return
    }
    while let presented = top.presentedViewController { top = presented }
    let controller = Gow3GameViewController()
    controller.modalPresentationStyle = .fullScreen
    // Without this, the presenting React Native controller keeps deciding the status bar.
    controller.modalPresentationCapturesStatusBarAppearance = true
    top.present(controller, animated: false) {
      controller.view.layoutIfNeeded()
      completion(controller)
    }
  }
}
