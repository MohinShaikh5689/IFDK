// SPDX-License-Identifier: GPL-3.0-or-later
// The native fullscreen view the GPU draws into: a CAMetalLayer-backed view in a view controller
// presented over React Native, which stays idle behind it. The MoltenVK screen test uses it now,
// and the game's Vulkan presenter will later.
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

  override var prefersStatusBarHidden: Bool { true }
  override var prefersHomeIndicatorAutoHidden: Bool { true }

  // The drawable follows the view in pixels (MoltenVK sizes the swapchain from it).
  override func viewDidLayoutSubviews() {
    super.viewDidLayoutSubviews()
    let scale = view.window?.screen.nativeScale ?? UIScreen.main.nativeScale
    metalView.metalLayer.contentsScale = scale
    metalView.metalLayer.drawableSize = CGSize(width: view.bounds.width * scale, height: view.bounds.height * scale)
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
    top.present(controller, animated: false) {
      controller.view.layoutIfNeeded()
      completion(controller)
    }
  }
}
