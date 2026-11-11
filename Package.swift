// swift-tools-version: 5.9
import PackageDescription

// Thin SwiftPM discovery entry. The C++ core stays in the native layout;
// Swift sources and tests are one integration, without a second core copy.
let package = Package(
    name: "RBush",
    products: [.library(name: "RBush", targets: ["RBush"])],
    targets: [
        .target(
            name: "CRBush",
            path: ".",
            exclude: ["benchmarks", "docs", "examples", "integrations", "scripts", "tests"],
            sources: ["bindings/c/src/rbush_c.cpp"],
            publicHeadersPath: "bindings/c/include",
            cxxSettings: [.headerSearchPath("include")]
        ),
        .target(
            name: "RBush",
            dependencies: ["CRBush"],
            path: "integrations/swift/Sources/RBush"
        ),
        .testTarget(
            name: "RBushTests",
            dependencies: ["RBush", "CRBush"],
            path: "integrations/swift/Tests/RBushTests"
        )
    ],
    cxxLanguageStandard: .cxx17
)
