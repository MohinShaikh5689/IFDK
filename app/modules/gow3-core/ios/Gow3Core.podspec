Pod::Spec.new do |s|
  s.name           = 'Gow3Core'
  s.version        = '0.1.0'
  s.summary        = 'gow3-ios native core: JIT pool, FEXCore guest CPU, self-test'
  s.description    = 'Native core of the God of War III (PS4) iOS port, exposed to the Expo shell.'
  s.author         = 'gow3-ios'
  s.homepage       = 'https://github.com/'
  s.license        = { :type => 'GPL-3.0-or-later' }
  s.platforms      = { :ios => '17.0' }
  s.source         = { git: '' }
  s.static_framework = true
  s.swift_version  = '5.9'

  s.dependency 'ExpoModulesCore'

  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
  }

  s.source_files = '*.swift'
  # Built by native/scripts/build-ios.sh and build-rppairing-ios.sh before `expo prebuild` (CI does this).
  s.vendored_libraries = 'lib/libgow3core_all.a', 'lib/libgow3_rppairing.a'
  s.libraries = 'c++'
  # The Rust pairing library draws randomness from Security; MoltenVK (in libgow3core_all.a) needs
  # Metal, QuartzCore (CAMetalLayer), IOSurface and CoreGraphics.
  s.frameworks = 'Security', 'Metal', 'QuartzCore', 'IOSurface', 'CoreGraphics'
  # StikJIT's universal script (MPL-2.0), sent to StikDebug when enabling JIT, and the licences of
  # StikJIT and idevice (MIT), which the JIT helper and the pairing library contain.
  s.resource_bundles = { 'Gow3Core' => ['jit/universal.js', 'jit/LICENSE-StikJIT-MPL-2.0.txt', 'jit/LICENSE-idevice-MIT.txt'] }
end
