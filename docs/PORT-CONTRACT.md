# Native port contract

## Primary library

This is an independent C++17 library, not a JavaScript fork distribution and not a Swift-first project. One faithful C++ RBush implementation is the algorithm authority.

- `include/`: public RBush and quickselect headers.
- `tests/`: core/C ABI fixtures and native parity transport.
- `benchmarks/`: native performance workloads and shared browser benchmark data/tools.
- `bindings/c/`: thin shared C ABI.
- `integrations/swift/`: Swift adapter source and tests.
- `integrations/wasm/`: WASM build bindings and language adapter.
- `examples/web/`: browser visualization, test and benchmark interface.
- Root `Package.swift`: thin SwiftPM discovery entry reusing the same C++ source; Swift implementation is not a core component.

## Faithfulness

RBush pin: `e59712716a84dda2e31f9b471b32faa178d8056f`.
quickselect pin: `0711310d28fe5f8a5ca75c721626d8e49feb9ccb`.

Preserve original constructor behavior, insert/remove/clear/all/search/collides/load,
custom bbox/comparison hooks, reference/equality removal, toJSON/fromJSON tree
structure, split/condense, OMT/STLT, and the literal quickselect/Floyd–Rivest flow.
No alternative R-tree, nth_element substitution, COW, or new index algorithm is
introduced during the faithful port phase.

C++ shared node references retain original graph habits. Language-only adapters
are documented explicitly: Swift and WASM serialize node structure as snapshots,
not a live arbitrary-JS-mutable graph. Do not claim identical object models.

## Verification inputs

Original JavaScript, tests, benchmark and visualization sources are not vendored
or committed here. `scripts/upstream.mjs` obtains exact pinned references in an
external cache; `ensureUpstream()` returns `{rbushRoot, quickselectRoot}` and
`loadOriginalRBush()` loads the oracle. Temporary adapted upstream tests also
stay outside this repository. Required licenses and origin links remain here.

Original tests change only their imports when targeting native/WASM adapters.
Compare full node order, bounds and height where upstream does, plus ordered
item identities, collision answers and explicit adapter lifetime regressions.
Only relevant library tests run; no MateText full test suite or device actions.

The C++ core is shared by native C++, C ABI, Swift and actual WASM. Browser
JavaScript draws exported data and provides controls; it does not substitute a
JavaScript tree in WASM mode. Diagnostic node export happens on changes, not
on every animation frame.
