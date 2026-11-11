# RBush Native · C++17

本项目是 [mourner/rbush](https://github.com/mourner/rbush) 的独立 C++17 移植仓库，**由 OpenAI Codex 负责一对一忠实移植**。原作者与算法版权归 Volodymyr Agafonkin 及原项目贡献者所有；RBush 的 MIT 许可与 quickselect 的 ISC 许可均保留。

This is an independent, faithful, function-by-function C++17 port of [RBush](https://github.com/mourner/rbush). **OpenAI Codex is responsible for the port.** Original authorship and license notices remain intact.

## 移植范围

以固定上游版本为算法基线，保留 `all/search/collides/load/insert/remove/clear/toJSON/fromJSON`、OMT/STLT、split/condense、默认容量 9、quickselect/Floyd–Rivest、引用身份与重复项行为。C ABI、Swift 和 WebAssembly 使用同一个 C++ 内核。首版不加入 COW、并发快照、其他索引或替代算法。

仓库只跟踪本项目的原生实现、包装层和适配后的测试、bench、浏览器 demo。原版 JavaScript、测试、bench 和 viz 由工具按固定 commit 获取到**仓库外的缓存**，作为差分验证输入，不在这里重复存放。来源、版本和许可证见 [UPSTREAM](docs/UPSTREAM.md)。Swift 是其中一个集成层，源码与测试位于 `integrations/swift`。根目录 `Package.swift` 仅作为标准 SwiftPM 的 Git URL 发现入口，直接编译同一份 C++ 核心，不另存一份核心或组织全仓库。

当前已实现 C++ 内核、C ABI、Swift Package 和实际 WASM demo；迁移验证与已有行为证据见 [验证记录](docs/VERIFICATION.md)。浏览器可见交互尚未完成人工验收，也尚未接入 MateText 生产索引。

## C++ 核心与目录

| 位置 | 职责 |
|---|---|
| [include](include/) | C++17 RBush 与 quickselect 核心，header-only。 |
| [bindings/c](bindings/c/) | 薄 C ABI，供跨语言调用。 |
| [tests](tests/) / [benchmarks](benchmarks/) | 原生测试、差分适配和性能 workload。 |
| [integrations/swift](integrations/swift/) | Swift 包装与聚焦测试。 |
| [integrations/wasm](integrations/wasm/) | 编译同一核心的 WebAssembly 绑定。 |
| [examples/web](examples/web/) | 浏览器验证与可视化示例。 |

C++ 核心只需 C++17 编译器。CMake 3.20+ 用于构建 C ABI、原生测试和 bench；Node.js 22+ 用于差分工具和浏览器示例，不是 C++ 核心依赖。

```bash
git clone https://github.com/videni/rbush-native.git ~/Workspace/rbush-native
export RBUSH_REPO="$HOME/Workspace/rbush-native"
bash "$RBUSH_REPO/scripts/test-native.sh"
```

核心可直接使用 [rbush.hpp](include/rbush.hpp)。CMake consumer 可链接 `rbush::core`；需要 C ABI 时链接 `rbush::c`，接口见 [rbush.h](bindings/c/include/rbush.h)。

```cpp
#include "rbush.hpp"
#include <memory>

rbush::RBush<> tree;
auto item = std::make_shared<rbush::Item>();
item->minX = 0; item->minY = 0;
item->maxX = 100; item->maxY = 60;
tree.insert(item);
auto hits = tree.search(rbush::Box{10, 10, 20, 20});
tree.remove(item);
```

## Swift 集成

Swift 包装使用 Swift tools 5.9+。通过本仓库 Git URL 引用 `RBush` product；根 manifest 是薄入口，实际包装位于 [integrations/swift](integrations/swift/)，C target 直接编译共享 C ABI 与核心。无需单独复制 C++ 或安装 npm 依赖。

```bash
swift test --package-path "$RBUSH_REPO" --jobs 3
```

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

## WebAssembly 与可视化

安装并激活官方 Emscripten 4.0.20，使 `em++` 位于 PATH，或将 `EMXX` 指向可执行文件。工具链放在仓库外。然后执行：

```bash
bash "$RBUSH_REPO/scripts/wasm-build.sh"
node "$RBUSH_REPO/scripts/parity-upstream.mjs" wasm
python3 "$RBUSH_REPO/scripts/serve.py"
```

打开 [本地可视化](http://127.0.0.1:8128/examples/web/)。服务器将外部上游缓存映射到 `/__reference/rbush/` 和 `/__reference/quickselect/`，默认仅监听 `127.0.0.1`。JS / C++ WASM 切换、uniform / cluster 数据、5k / 10k / 50k / 100k、树层级、查询高亮和 benchmark 面板沿用原版验证习惯。WASM 模式的树操作和 bounds 来自实际 C++，Canvas 绘制由浏览器完成。生成的 WASM 产物不提交；详见 [WASM 使用说明](integrations/wasm/README.md)。

## 语言适配与所有权

| 层 | 行为与边界 |
|---|---|
| C++ | 原操作、稳定排序、引用身份、自定义 equality、虚方法 hooks 和共享节点图的 `toJSON/fromJSON`；容量以整数表达。 |
| C ABI | opaque handle、批量 POD、显式释放；单次查询生成独立 owned buffer。异常转为错误码，非法节点结构在进入核心前拒绝。比较器 callback/context 由调用方保活，替换或销毁后可释放。 |
| Swift | `RBushItem` 为引用类型，相同对象可重复出现；默认移除一个同身份项。bbox/payload 可变，采用 remove → 修改 bbox → insert。修改操作返回 `Self`，操作可抛出错误，泛型 payload 与 bbox 投影分开。 |
| Swift 节点 | `toJSON` 返回保留顺序、bbox 和高度的结构快照值，叶 item 保持身份；不自动编码 JSON 文本，不共享节点容器别名。`fromJSON` 直接导入结构，不扁平化重建。 |
| 浏览器 WASM | 对象身份、自定义 equality 与比较器回调进入原生核心。`data/toJSON` 是诊断快照，修改后须 `fromJSON` 导回；坐标通过 remove → 修改 → insert 重新投影。 |

可变 tree 的并发访问由调用方协调。分配或内部错误不承诺事务回滚，出错后应清理或重建；结构校验失败保留原树。节点快照、跨语言对象模型差异不作为算法能力变化。

## 差分测试与 bench

先按固定版本获取仓库外的上游参考。验证工具只替换原版测试的导入目标，分别运行 native CLI 和实际 WASM；另外比较完整树形、顺序、bbox、查询结果及内存增长。原版 benchmark workload 有对应的原生/适配版本。

```bash
node "$RBUSH_REPO/scripts/fetch-upstream.mjs"
node "$RBUSH_REPO/scripts/parity-upstream.mjs" original
export RBUSH_PARITY_CLI="$RBUSH_REPO/build-native/rbush_parity_cli"
node "$RBUSH_REPO/scripts/parity-upstream.mjs" native
node "$RBUSH_REPO/scripts/parity-source.mjs"
node "$RBUSH_REPO/scripts/parity-sequences.mjs" native
node "$RBUSH_REPO/scripts/parity-sequences.mjs" wasm
node "$RBUSH_REPO/scripts/parity-datasets.mjs" native
node "$RBUSH_REPO/scripts/parity-datasets.mjs" wasm
node --expose-gc "$RBUSH_REPO/scripts/parity-adapter-regressions.mjs"
node "$RBUSH_REPO/scripts/parity-bench.mjs"
"$RBUSH_REPO/build-native/rbush_bench" 100000
"$RBUSH_REPO/build-native/rbush_bench" suite
```

固定随机种子便于对照。原生核心、Swift 包装、WASM 桥接与浏览器绘制耗时分别解释，不将这些结果当作 iPad App 的性能承诺。


## 上游对齐与本次移植性能

原版JS、C++和WASM已经按固定版本做功能/树形差分；Swift也提供 `toBBox`、`compareMinX/Y` 自定义closure与reset接口，使用现有C hooks，不另写算法。语言模型差异仍以本文说明为准，不声称所有语言层完全等价。

[同一画布数据的性能报告](docs/PERFORMANCE.md) 使用前面相同的16份数据/查询，分别运行原版JS和本次C++，不是另换随机workload。结果包含全部384个条件及输入SHA；并非iPad App端到端性能。
