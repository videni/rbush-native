# Faithful port contract

Approved user scope: C++17 one-to-one translation of RBush, including upstream tests/bench/viz; Swift package in same new repository; WASM builds the same core. README attribution requested explicitly. No algorithm redesign or product integration in this port phase.

Pinned RBush: e59712716a84dda2e31f9b471b32faa178d8056f. Pinned quickselect: 0711310d28fe5f8a5ca75c721626d8e49feb9ccb.

## Ownership

- Core worker: cpp/include/rbush.hpp and cpp/include/quickselect.hpp. Preserve constructor min/max entries, insert/remove/clear/all/search/collides/load, toBBox/compare customization, toJSON/fromJSON structure, split/condense/OMT/STLT, reference/equality removal. Publish concrete public interface to adapter worker early.
- Adapter worker: cpp/include/rbush.h, cpp/src/rbush_c.cpp, Package.swift, Sources/RBush/, Tests/RBushTests/. C ABI must expose batch buffers and node export/import enough to preserve tree structure. Do not hide algorithm changes in adapter.
- Parity/WASM worker: cpp/test/, cpp/bench/, wasm/, scripts/parity*, scripts/wasm*, new browser demo assets. Use original test names/data and original visualization conventions. Tell root CMake target needs. Do not edit upstream test/bench/viz originals.
- Root: README, LICENSE/provenance, CMakeLists.txt, root build scripts and CI, final integration/testing and local commits. No production App integration yet.

## Integration principles

C++ core is the algorithm authority. No COW, concurrency, alternate split heuristics, nth_element replacement, or unrequested indexes. Preserve original reference/equality behaviors in C++ and document unavoidable value/identity adapters explicitly.

Native and WASM run equivalent operation sequences against pinned JS oracle, checking query result identities, constructor defaults, boundary cases, and tree structure where upstream asserts it. Test degenerate/empty boxes per upstream; do not carry Hash Grid's non-empty policy into RBush.

ABI and Swift API should preserve upstream operation names. No Swift objects retained inside C++ algorithm; bridge owns payload mapping if needed. Node exports are diagnostic and only requested on changes, not every animation frame. Browser Canvas drawing stays JavaScript; all tree operations and bounds come from WASM in WASM mode.
