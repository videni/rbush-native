# Verification record

Reference: RBush e59712716a84dda2e31f9b471b32faa178d8056f; quickselect 0711310d28fe5f8a5ca75c721626d8e49feb9ccb. Emscripten 4.0.20. All checks are scoped to this new library; no MateText suite or physical-device commands.

| Scope | Evidence |
|---|---|
| Original JavaScript | npm test: 26/26 pass; upstream files unchanged |
| Native original tests | Original 26 tests, only RBush import changed: 26/26 pass; root Release CMake artifact independently passes |
| WASM original tests | Same original 26 tests against actual .wasm: 26/26 pass |
| Exact tree sequences | 90 checkpoints each native/WASM; 3 seeds and capacities 4/9/16; node order, heights, bbox, ordered identities and collides match |
| Canvas scales | uniform/cluster × 5000/10000/50000/100000; full trees and ordered queries match pinned JS |
| Memory growth | WASM heap grew 16.9→50.6MB; heap views refreshed and parity retained |
| Swift wrapper | 11 baseline tests pass; after import-validation review fix, 2 focused fromJSON tests pass (1 new, 1 overlapping baseline); no unrelated suite rerun |
| C/C++ smoke | Direct strict-warning builds and ASan/UBSan pass; root CTest Release core_smoke + c_adapter pass with asserts explicitly active |
| Adapter regressions | Zero capacity clamps4; actual custom comparison call counts/shape match; WeakRef/GC release on clear/remove/import/dispose/unknown removals/duplicates pass |
| Benchmarks | Original perf and four microbenchmark workloads ported; native/JS/WASM checksums match at5k/10k/50k/100k; native original1M perf also ran |
| Browser surface | HTML/JS/WASM and local server exist at localhost:8127/wasm; visual/UI automation could not navigate because browser-control API failed before navigation. Visible browser acceptance remains unverified |

## Review fixes

- C++ _splitRoot NodePtr alias: passing the JS reference value by C++ reference changed it during root replacement. Corrected to NodePtr by value, with a RED→GREEN same-height merge regression.
- C weak identity bookkeeping retained last removed pointer through cleanup. Drop temporary requested pointer before checking weak expiration; private lifecycle fixture proved RED on the previous implementation, GREEN with sanitizer.
- C ABI malformed import could make leaf height2 and then index beyond insertPath. Stage and validate leaf/branch/height invariants; ASan repro now rejects import and subsequent insert is safe. Swift focused import regression passes.
- WASM constructor0 no longer substitutes9; custom comparators reach real C++ virtual hooks; removed payload maps are released. Targeted regression tests preserve original test expectations.

## Explicit language adapter differences

C++ core exposes shared NodePtr graphs as original object references. Swift/WASM exports serialize structure as snapshots; they do not expose a live arbitrary-JS-mutable node graph. This is explicitly documented, not silently presented as JavaScript drop-in object-model identity. No O(N) query resynchronization or replacement tree algorithm was added to imitate live JS mutation.

Root review also kept Release assert tests enabled, included the private C bookkeeping fixture without duplicate linking, and set CI's native CLI path explicitly. Native/WASM test transports and real production adapters are described in wasm/README.md.
