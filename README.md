# RBush C++ / Swift / WebAssembly

This is a faithful, function-by-function C++17 port of [RBush](https://github.com/mourner/rbush), with Swift and WebAssembly adapters. **OpenAI Codex is responsible for the port.** Original authorship and MIT licensing are preserved.

本项目是 [mourner/rbush](https://github.com/mourner/rbush) 的原生语言移植项目，**由 OpenAI Codex 负责移植**。

## 移植说明

本项目遵循“原封不动、一对一忠实移植”的原则：以固定版本的原版 RBush 源码为唯一算法基线，保留原版 API 习惯、算法步骤、边界行为、测试、性能基准（bench）与可视化；不自行重新设计或替换树算法。编程语言必需的类型、内存所有权与 FFI 适配会单独说明，不冒充原版已经具备的能力。

原作者与算法版权归原项目作者所有。本项目保留原版 MIT 许可、版权声明和来源链接；Codex 的说明用于交代移植来源，不取代原作者署名。

- RBush 上游：[e59712716a84dda2e31f9b471b32faa178d8056f](https://github.com/mourner/rbush/tree/e59712716a84dda2e31f9b471b32faa178d8056f)。
- 原版 README：[README.upstream.md](README.upstream.md)。
- 根目录 `index.js`、`test/`、`bench/`、`viz/` 保留原版，用作行为参考与差分验证。
- 新库内提供 C++17 内核、薄 C ABI、Swift 包装和 `Package.swift`；Swift 包装不放入 MateText 私有业务代码。
- 同一 C++ 内核编译为 WebAssembly，由浏览器可视化调用实际 WASM，不展示另一套 JS 树冒充原生实现。

## 当前状态

C++ 内核、C ABI、同仓库 Swift Package 和 WebAssembly 已实现。已完成原版测试与差分验证，详见 [验证记录](docs/VERIFICATION.md)。浏览器视觉/交互人工验收尚未完成；尚未接入 MateText 生产索引。

## 开发边界

首版先忠实移植，再讨论额外能力。原版没有的 COW、并发快照、增量密度摘要等不得悄悄加入核心；业务接入另行验证。原生性能、WASM 性能和浏览器绘制耗时分别测量。


## 快速开始

C++ 核心为 `cpp/include/rbush.hpp`，默认容量 9、最小容量 4。C ABI 位于 `cpp/include/rbush.h`；Swift 包装位于 `Sources/RBush/`，两者使用同一个核心。

```bash
npm ci --ignore-scripts
scripts/test-native.sh
RBUSH_PARITY_CLI="$PWD/build-native/rbush_parity_cli" node scripts/parity-upstream.mjs native
swift test --jobs 3
```

Swift 项目通过本仓库的 `Package.swift` 依赖 `RBush` product：

```swift
import RBush

let tree = try RBush<String>()
let item = RBushItem(
    BoundingBox(minX: 0, minY: 0, maxX: 100, maxY: 60),
    payload: "item-1"
)
try tree.insert(item)
let hits = try tree.search(BoundingBox(minX: 10, minY: 10, maxX: 20, maxY: 20))
try tree.remove(item)
```

浏览器使用同一 C++ 核心的 WASM 产物：

```bash
# 安装并激活官方 Emscripten 4.0.20，或设置 EMXX 指向 em++
scripts/wasm-build.sh
node scripts/parity-upstream.mjs wasm
python3 -m http.server 8127
```

打开 [本地可视化](http://localhost:8127/wasm/)。提供原版 JS / C++ WASM 切换、uniform / cluster、5k / 10k / 50k / 100k、插入/批量插入/删除、树层级与查询高亮、测试和 benchmark 面板。构建产物不提交，使用前需要生成 `wasm/dist/`。完整说明见 [wasm/README.md](wasm/README.md)。

## 保真范围与语言适配

| 层 | 行为与边界 |
|---|---|
| C++ 核心 | 逐函数移植 RBush 和 quickselect；保留 OMT/STLT、split/condense、稳定排序、引用身份与自定义 equality、虚方法 hooks，以及共享节点图的 toJSON/fromJSON。容量采用整数；C++ 虚方法覆盖对应 JS 的自定义方法。 |
| C ABI | opaque handle、批量 POD、显式释放；单次查询结果使用独立 owned buffer。异常转换为错误码，非法节点结构在进入核心前拒绝。 |
| Swift | 引用类型 item 保留身份与重复出现；bbox/payload 可变，移除后改 bbox 再插回。操作可抛出错误，修改操作返回 Self；自定义数据放入泛型 payload，bbox 在边界投影。 |
| Swift 节点导出 | toJSON 返回结构快照值，叶 item 保持身份；不自动编码 JSON 文本，不保留 JS 式节点容器共享别名。fromJSON 保留节点结构，不扁平化重建。 |
| 浏览器 WASM 适配 | 操作调用真正的 C++；对象 identity、equality、比较器回调保留。data/toJSON 是诊断快照，修改快照需 fromJSON 导回；不是原 JS 可任意修改的 live data 对象。原对象坐标在移除、修改、重插时重新投影。 |

C++ 核心保留原算法；语言适配差异明确列出，不声称所有语言的对象模型完全等价。可变 tree 的并发访问需调用方协调。分配/内部错误不承诺事务回滚；原生边界出错后应清理或重建，结构校验失败则保留原树。

## 测试与性能基准

保留原版 `test/`、`bench/`、`viz/`。原版 26 项测试仅改 import，分别指向 native CLI 和实际 WASM；另核对完整树形、顺序、bbox、查询结果与内存增长。

```bash
node scripts/parity-sequences.mjs native
node scripts/parity-sequences.mjs wasm
node scripts/parity-datasets.mjs native
node scripts/parity-datasets.mjs wasm
node --expose-gc scripts/parity-adapter-regressions.mjs
node scripts/parity-bench.mjs
build-native/rbush_bench 100000
build-native/rbush_bench suite
```

基准保留原 workload，固定随机种子便于对照；原生核心、Swift 包装、WASM 桥接与浏览器绘制耗时分别解释。没有将 JavaScript 或 WASM 的结果当成 iPad App 的提升承诺。
