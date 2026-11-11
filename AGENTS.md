# RBush native port rules

- User requires a faithful, function-by-function C++17 port of pinned upstream RBush. Preserve algorithms and API habits. Do not substitute another R-tree or optimize/rewrite the algorithm.
- Preserve original index.js, test/, bench/, viz/, LICENSE and README.upstream.md as reference. Preserve quickselect/Floyd–Rivest algorithm too; do not replace with std::nth_element.
- Swift wrapper and Package.swift live in this repository. C ABI / Swift / WASM are thin adapters, with language differences documented.
- README must name original project and state that OpenAI Codex is responsible for this port, as explicitly requested. Git author/commit messages must not use Codex attribution.
- Work only on owned files, do not revert other agents. Do not touch MateText/DrawKit source or physical devices. No device commands, automatic restores, or account copies.
- Focus tests on this new library; no MateText full test suite. Record exact validation and failures. No remote push unless explicitly requested separately.
