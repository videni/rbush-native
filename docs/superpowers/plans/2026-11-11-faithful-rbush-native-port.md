# Faithful RBush Native Port Implementation Plan

> **For agentic workers:** Execute owned components in parallel; use targeted TDD and verify matching library scope only.

**Goal:** Port pinned RBush one-to-one to C++17; provide C ABI, same-repo Swift package, preserved tests/bench and browser visualization using the same core compiled to WASM.

**Architecture:** Header-based C++ algorithm with original method structure and quickselect. Thin C and Swift adapters. Upstream JavaScript remains oracle. Browser drawing is adapted around WASM core, not a second implementation.

**Spec:** docs/PORT-CONTRACT.md and explicit user instructions.

## Global Constraints

- No algorithm redesign, COW, extra caches/indexes, or App source integration in the faithful port phase.
- Preserve upstream source/tests/bench/viz/license; document attribution and language-only differences.
- No physical device commands. No remote push. Only relevant new-library tests.

## Tasks

- [x] Core: port all RBush and quickselect functions; publish interface; focused compile/behavior tests.
- [x] C ABI + Swift: preserve operational API, identity rules, ownership, import/export; swift test only this package.
- [x] Tests + bench + WASM: port upstream cases and data, differential JS oracle; compile same core with Emscripten, verify browser visualization.
- [ ] Root: README/provenance/builds; integrate independent deliverables; run library-scoped native/Swift/WASM checks; review source correspondence and report limitations.

## Review Focus

- Duplicate/equal boxes and reference identity removal must match original.
- Degenerate boxes remain supported where original supports them.
- Stable ordering, comparator ties, quickselect partitioning and tree shape should not silently change.
- C/Swift/WASM lifetime and output buffers remain valid without leaking mutable internals.
- Browser WASM visualization must reflect native tree, not accidentally call upstream JS tree.

## Ledger

- User explicitly requested parallel agents and README attribution; supersedes earlier no-delegation steering for this new library.
- Fresh independent repository cloned from upstream; native port branch isolates changes. Swift packaging is part of this library.

- Core worker and independent reviewer found no alternative tree algorithm; original reference files retained.
- Native and WASM original tests26/26 each,90 exact checkpoints each,8 scale/distribution datasets each, Swift11 baseline +2 focused import checks, C/C++ sanitizers and root2 CTest targets pass.
- Review findings fixed in one pass: reference-parameter root alias, malformed import OOB, comparator forwarding, zero capacity default, payload bookkeeping. No dropped tests or weakened expectations.
- Ruling: C++ NodePtr preserves original live aliasing. Swift/WASM export uses structural snapshots across language boundary, documented explicitly; native algorithm remains unchanged, no per-query full-tree synchronization added. Cost: callers needing JS live-node mutations must explicitly import the changed snapshot.
- Browser tool could not navigate generated tab (request-policy/timeout interface errors); no visual screenshot claim. Local browser server remains available8127; actual WASM tested in Node and its source is the same core.
