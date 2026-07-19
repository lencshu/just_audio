#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html
#
Pod::Spec.new do |s|
  s.name             = 'just_audio'
  s.version          = '0.0.1'
  s.summary          = 'Flutter audio player'
  s.description      = <<-DESC
A flutter plugin for playing audio.
                       DESC
  s.homepage         = 'https://github.com/ryanheise/just_audio'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Your Company' => 'email@example.com' }
  s.source           = { :path => '.' }
  # Includes the Darwin-shared AudioEffects module: pure-C++ DSP (.cpp/.hpp)
  # plus the Objective-C++ tap/controller layer (.mm).
  s.source_files = 'just_audio/Sources/just_audio/**/*.{h,m,mm,cpp,hpp}'
  # Exclude host-only unit tests from the plugin build.
  s.exclude_files = 'just_audio/Sources/just_audio/AudioEffects/Tests/**/*'
  s.public_header_files = 'just_audio/Sources/just_audio/include/**/*.h'
  s.ios.dependency 'Flutter'
  s.osx.dependency 'FlutterMacOS'
  s.ios.deployment_target = '12.0'
  s.osx.deployment_target = '10.14'
  # MediaToolbox: MTAudioProcessingTap. AudioToolbox: PCM buffer/format types.
  s.frameworks = 'AVFoundation', 'MediaToolbox', 'AudioToolbox'
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_CXX_LIBRARY' => 'libc++'
  }
end
