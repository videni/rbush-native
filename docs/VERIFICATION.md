# Verification record

Reference: RBush `e59712716a84dda2e31f9b471b32faa178d8056f`; quickselect `0711310d28fe5f8a5ca75c721626d8e49feb9ccb`. WASM toolchain: Emscripten 4.0.20. Checks are scoped to this library; no MateText suite or physical-device commands.

## C++ primary layout · 2026-11-11

The core is in `include`, with the thin C ABI in `bindings/c`, native tests/benchmarks in `tests`/`benchmarks`, integrations in `integrations`, and the browser example in `examples/web`. The root `Package.swift` remains only a standard Git-URL discovery entry. Its C target compiles the shared C binding, adding the root core header search path; Swift sources/tests have explicit integration paths.

After this source-path change, `swift test --jobs 3` rebuilt the package and passed **12/12 tests**, without Swift logic changes or package warnings. This packaging check did not repeat the C++/WASM algorithm suites. Earlier relocation evidence below predates this directory-only change.

## Independent repository relocation · 2026-11-11

The implementation moved to the independent `videni/rbush-native` repository. Original JavaScript, tests, bench and viz are external pinned inputs, fetched by `scripts/fetch-upstream.mjs`; no original source tree or npm dependency installation is required inside this repository. CI separately runs native, Swift and WASM validation, installing its WASM toolchain outside the checkout.

| Scope | Evidence in the relocated repository |
|---|---|
| Swift packaging | Fresh `swift test --jobs 3`, dedicated local `.build`: **12/12 pass**, including identity/duplicates, equality order, degenerate boxes, exact structural snapshot import/export, malformed import rejection, mutation workflow and payload release. |
| Reference retrieval | Explicit fetch command is idempotent; exact pinned HEAD and clean tracked contents checked. Original unmodified Node test suite runs from external cache: **26/26 pass**. |
| WASM relocation | Fresh Emscripten 4.0.20 build using an external toolchain. Original tests **26/26**, **90** exact-tree checkpoints and **8** uniform/cluster dataset cohorts pass. Constructor/comparator/GC regressions pass, including **456** matching comparator callback invocations. |
| Web server | `127.0.0.1:8128`: app, `.wasm` and external `/__reference/` URLs return 200; WASM MIME/magic and reference SHA checked. Path escapes return 403. |
| Browser acceptance | Browser navigation remains blocked by the control environment. Visible UI and human interaction acceptance remain **unverified**. |

The remaining table records validation of the same implementation before the packaging relocation. These results establish the prior behavior baseline; they do not substitute for checks of the relocated scripts, cache paths or browser server.

## Implementation baseline before relocation

| Scope | Evidence |
|---|---|
| Original JavaScript | Original 26 tests pass. |
| Native original tests | Original 26 tests, only RBush import changed: 26/26 pass; independent root Release CMake artifact also passes. |
| WASM original tests | Same original 26 tests against actual `.wasm`: 26/26 pass. |
| Exact tree sequences | 90 checkpoints each native/WASM; 3 seeds and capacities 4/9/16; node order, heights, bbox, ordered identities and collides match. |
| Canvas scales | uniform/cluster × 5000/10000/50000/100000; full trees and ordered queries match pinned JS. |
| Memory growth | WASM heap grew 16.9→50.6 MB; heap views refreshed and parity retained. |
| C/C++ validation | Strict-warning builds and ASan/UBSan pass; Release CTest core_smoke + c_adapter pass with asserts explicitly active. |
| Adapter regressions | Explicit zero capacity clamps to 4; actual custom comparison call counts/shape match; WeakRef/GC release on clear/remove/import/dispose/unknown removals/duplicates pass. |
| Benchmarks | Original perf and four microbenchmark workloads ported; native/JS/WASM checksums match at 5k/10k/50k/100k; native original 1M perf also ran. |
| Browser surface | HTML/JS/WASM existed; browser automation failed before navigation. No visible UI acceptance was claimed. |

## Review fixes and regression evidence

- C++ `_splitRoot` NodePtr alias: passing the JS reference value by C++ reference changed it during root replacement. Corrected to NodePtr by value, with a RED→GREEN same-height merge regression.
- C weak identity bookkeeping retained the last removed pointer through cleanup. Drop the temporary requested pointer before checking weak expiration; the private lifecycle fixture proved RED on the previous implementation and GREEN under sanitizers.
- C ABI malformed import could make a leaf have height 2, then index beyond insertPath. Staged import now validates leaf/branch/height invariants. The ASan repro rejects import and subsequent insertion is safe; the Swift public import regression also passes.
- WASM explicit constructor 0 no longer substitutes 9. Custom comparators reach real C++ virtual hooks and survive clear/import. Removed payload maps are released. Targeted tests retain the original expectations.

## Explicit language adapter differences

C++ exposes shared NodePtr graphs corresponding to original object references. Swift/WASM exports are structural snapshots; they do not expose a live arbitrary-JS-mutable node graph. Swift leaf items preserve identity. WASM payloads preserve identity, equality and comparator callbacks, while coordinates are projected across the ABI using remove → modify → insert. Snapshot mutation requires explicit `fromJSON` import.

No O(N) query resynchronization or replacement tree algorithm was added to imitate live JS mutation. Query results are produced by a single core query and transferred in owned batch buffers. Native/WASM test transports and production adapters are described in the [WASM documentation](../integrations/wasm/README.md). Native core, wrapper/ABI, WASM bridge and browser rendering measurements are separate; none constitutes a measured iPad product improvement.


## Swift customization and same-input native comparison

- Swift custom bbox/compare closures added through existing C ABI, default callback bypass retained. Focused3tests RED→GREEN; full package15/15. 24item/cap4customcomparator matches pinned JS152calls, orderedarguments, fulltree and queryorder. NoC++algorithmchange.
- OriginalJS vs本次nativeC++ uses the SAME earlier canvas datasets/queries/radii/exclusions. 16serializedfixtures,384conditions,240queries each, full ID equality before timing; prior result totals match. See PERFORMANCE.md. No newrandomworkload mixed in.
