// swift-tools-version: 5.9
import PackageDescription
let package = Package(
    name: "RBush",
    products: [.library(name: "RBush", targets: ["RBush"])],
    targets: [
        .target(name: "CRBush", path: "cpp", sources: ["src/rbush_c.cpp"], publicHeadersPath: "include"),
        .target(name: "RBush", dependencies: ["CRBush"]),
        .testTarget(name: "RBushTests", dependencies: ["RBush", "CRBush"])
    ],
    cxxLanguageStandard: .cxx17
)
