# Browser tests, benchmarks and tree visualization

This independent repository contains our C++ / Swift / WASM port and adapted
tests, benchmarks and demo. Original RBush JavaScript, tests, benchmarks and
visualization sources are verification inputs fetched into an **external cache**,
not files vendored into this repository. The reference loader pins RBush to
`e59712716a84dda2e31f9b471b32faa178d8056f` and quickselect to
`0711310d28fe5f8a5ca75c721626d8e49feb9ccb`.

Build `bash scripts/wasm-build.sh` from the repository root after installing and
activating official Emscripten **4.0.20** outside the repository, or set `EMXX`
to your em++. The build script also detects `em++` on PATH, `EMSDK`, or the
external `$XDG_CACHE_HOME/rbush-native/emsdk` directory (default
`~/Library/Caches/rbush-native/emsdk` on macOS, `~/.cache/rbush-native/emsdk`
elsewhere). No global shell configuration is required. `integrations/wasm/dist/` contains
only our generated build output.

Run `python3 scripts/serve.py`, then open `http://127.0.0.1:8128/examples/web/`.
The older `/wasm/` link redirects to this page.
The server defaults to loopback; optional `--bind` and `--port` flags change
the listening address. It ensures the pinned external references are available
through `scripts/upstream.mjs`, then serves them at `/__reference/rbush/` and
`/__reference/quickselect/`. The browser's import map resolves the original
quickselect module through that reference route. No root npm install is needed.
Override `RBUSH_UPSTREAM_CACHE` to select another external source-cache directory.

The page preserves upstream's 701px canvas, uniform / 30-cluster generators,
100k initial items, insert/bulk-insert sizes, leftmost removal, and node depth
colors. It also offers 5k / 10k / 50k datasets, search highlighting, both-engine
tests, and benchmark output. It is a local library demo, not an application
integration or published website.

`integrations/wasm/bridge.cpp` only turns by-value structs into pointer parameters.
`scripts/wasm-build.sh` compiles `bindings/c/src/rbush_c.cpp` and includes the same
`include/rbush.hpp` and `include/quickselect.hpp` used by native/Swift. The C++
headers are the primary library; this directory is an optional integration.
The browser example lives under `examples/web/`, its portable JavaScript
benchmark workloads under `benchmarks/`, and its assertion harness under
`tests/browser/`. No JavaScript
tree exists in WASM mode. JS owns original item objects, maps reference identity
to a numeric C identity, copies inputs/results, and renders exported node bounds.
Queries execute one native tree traversal. Heap views are reacquired after
allocations so growth does not leave stale typed arrays. Node export is only
requested after mutations, not on every frame or search.

## Validation commands

- `node scripts/parity-upstream.mjs native` — original 26 tests against C ABI CLI.
- `node scripts/parity-upstream.mjs wasm` — same tests against actual `.wasm` in Node.
- `node scripts/parity-sequences.mjs native` / `wasm` — 90 exact-tree checkpoints,
  plus ordered result identities and collision queries against pinned upstream.
- `node scripts/parity-datasets.mjs native` / `wasm` — full-tree and ordered-query
  parity for 5k / 10k / 50k / 100k uniform and cluster datasets; the WASM run
  explicitly forces memory growth before accessing the tree again.
- `node --expose-gc scripts/parity-adapter-regressions.mjs` — explicit zero
  capacity, custom comparator structure/call counts, and payload release through
  clear / remove / import / dispose / unknown removal / duplicate occurrences.
- `node scripts/parity-bench.mjs` — native / WASM / pinned-JS benchmark query
  checksums at all four canvas sizes and all four original microbench workloads.
  Set `RBUSH_BENCH` to select a separately built native benchmark executable.
- `node scripts/parity-source.mjs` — function-name correspondence presence gate;
  this is not a substitute for algorithm review.

`RBUSH_PARITY_CLI` can point at a separately built `rbush_parity_cli`; the default
is `build-native/rbush_parity_cli`. Its native adapter replays mutations in a fresh CLI
process for each observation. It is a test transport and is not benchmarked.
The original test file stays untouched in the external pinned cache. The test runner rewrites only its RBush
import. The browser additionally replaces Node's `test` and `assert` imports
with a minimal synchronous harness supporting exactly the assertions it uses;
the authoritative strict-assert runs remain the Node commands above.

## Benchmark adaptation

`benchmarks/perf.cpp` and `benchmarks/benchmark.mjs` port upstream
`bench/perf.js`'s operation sequence,
random-box formula, maxFill=16, and 1000 queries at each original box size.
Native defaults to the original 1M items; pass 5000, 10000, 50000, or 100000
for canvas-sized runs. Both ports replace Math.random with the same seed-42 LCG,
preserving input formulas while making data and search checksums reproducible.
The optional final clear/bulk-load sample also covers the bulk benchmark.
`rbush_bench suite` and the browser's microbench button also port the original
insert-100, bulk-load-10k, search-after-insert-10k, and search-after-bulk-load-10k
workloads from the four `.bench.js` files. Bulksearch retains its original
size-100 boxes. These are single timed samples, not Benchmark.js ops/sec statistics.
Native reports core timings; browser timings include the JS/C ABI bridge, and
are not directly comparable to native core-only times. Original benchmark
suite files stay in the external cache, including their extensionless imports
and array datasets; those old fixture-format issues are not silently edited.

## Language differences

The JS facade retains chainability, original object references, identity removal,
duplicate occurrences, custom equality, custom coordinate-field `toBBox`, full
node order/bounds/height, and infinities. WASM equality and comparator overrides
invoke the supplied JS callbacks synchronously from the original native
algorithm. Default coordinate comparisons stay entirely in C++. Callback slots
are released on replacement/disposal. Occurrence counts release JS payloads on
the last removal, clear, replacement import, and disposal; unknown removal
does not add a retained object. The native **test transport**
evaluates equality on its ordered `all()` results before sending a match set;
it precomputes comparator pair values before a custom-comparator mutation.
Callback side effects/call counts are therefore not parity-tested by that
transport. Actual WASM comparator call counts are tested against upstream.
C++ core hooks are also tested separately for custom native types/comparators.

The facade normalizes `toBBox` fields at the boundary. Changes to an object's coordinates
outside tree operations do not mutate its copied native geometry. `data` /
`toJSON()` return a full snapshot; mutating that returned snapshot only affects
the native tree after `fromJSON()`. The C++ core itself preserves direct
shared-node `toJSON` / `fromJSON` habits. Explicit `dispose()` releases a JS/WASM
tree; the demo uses it on reset and after temporary tests/benchmarks.
