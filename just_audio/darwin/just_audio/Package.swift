// swift-tools-version: 5.9
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "just_audio",
    platforms: [
        .iOS("12.0"),
        .macOS("10.14")
    ],
    products: [
        .library(name: "just-audio", targets: ["just_audio"])
    ],
    dependencies: [],
    targets: [
        .target(
            name: "just_audio",
            dependencies: [],
            // Host-only DSP unit tests are not part of the plugin build.
            exclude: [
                "AudioEffects/Tests"
            ],
            cSettings: [
                .headerSearchPath("include/just_audio")
            ],
            linkerSettings: [
                .linkedFramework("AVFoundation"),
                .linkedFramework("MediaToolbox"),
                .linkedFramework("AudioToolbox")
            ]
        )
    ],
    // The AudioEffects DSP core and tap layer are C++/Objective-C++.
    cxxLanguageStandard: .cxx17
)
