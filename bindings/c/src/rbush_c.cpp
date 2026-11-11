#include "rbush.h"
#include "rbush.hpp"
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using Tree = rbush::RBush<>;
class AdapterTree : public Tree {
public:
    using Tree::Tree;
    rbush_compare_fn compareX = nullptr, compareY = nullptr;
    void *compareContext = nullptr;
    double compareMinX(const ItemPtr &lhs, const ItemPtr &rhs) const override {
        return compareX ? compareX(lhs->id, rhs->id, compareContext) : Tree::compareMinX(lhs, rhs);
    }
    double compareMinY(const ItemPtr &lhs, const ItemPtr &rhs) const override {
        return compareY ? compareY(lhs->id, rhs->id, compareContext) : Tree::compareMinY(lhs, rhs);
    }
};
struct rbush_tree {
    AdapterTree tree;
    std::unordered_map<uint64_t, std::weak_ptr<rbush::Item>> identities;
    explicit rbush_tree(int maximum) : tree(maximum) {}
};
struct rbush_result { std::vector<rbush_item> items; };
namespace {
thread_local std::string lastError;
struct Invalid : std::runtime_error { using std::runtime_error::runtime_error; };
struct SmallBuffer : std::runtime_error { using std::runtime_error::runtime_error; };
template<class F> rbush_status guard(F &&operation) noexcept {
    try { operation(); lastError.clear(); return RBUSH_OK; }
    catch (const SmallBuffer &error) { lastError = error.what(); return RBUSH_BUFFER_TOO_SMALL; }
    catch (const Invalid &error) { lastError = error.what(); return RBUSH_INVALID_ARGUMENT; }
    catch (const std::bad_alloc &) { lastError = "allocation failed"; return RBUSH_ALLOCATION_FAILED; }
    catch (const std::exception &error) { lastError = error.what(); return RBUSH_INTERNAL_ERROR; }
    catch (...) { lastError = "unknown C++ exception"; return RBUSH_INTERNAL_ERROR; }
}
void require(bool condition, const char *message) { if (!condition) throw Invalid(message); }
rbush::Box box(rbush_box value) { return {value.min_x, value.min_y, value.max_x, value.max_y}; }
rbush_item item(const Tree::ItemPtr &value) {
    return {value->minX, value->minY, value->maxX, value->maxY, value->id};
}
Tree::ItemPtr project(rbush_tree &tree, rbush_item value) {
    auto &entry = tree.identities[value.identity];
    auto result = entry.lock();
    if (!result) { result = std::make_shared<rbush::Item>(); entry = result; }
    result->minX = value.min_x; result->minY = value.min_y;
    result->maxX = value.max_x; result->maxY = value.max_y; result->id = value.identity;
    return result;
}
struct Snapshot {
    std::vector<rbush_node> nodes;
    std::vector<rbush_item> items;
    std::vector<uint64_t> children;
    uint64_t append(const Tree::NodePtr &node) {
        const auto index = nodes.size();
        nodes.push_back({node->minX, node->minY, node->maxX, node->maxY,
                         0, static_cast<uint64_t>(node->children.size()), node->height, node->leaf ? 1 : 0});
        if (node->leaf) {
            nodes[index].child_offset = items.size();
            for (const auto &child : node->children) items.push_back(item(std::get<Tree::ItemPtr>(child)));
        } else {
            const auto offset = children.size();
            nodes[index].child_offset = offset;
            children.resize(offset + node->children.size());
            for (size_t i = 0; i < node->children.size(); ++i) {
                const auto childIndex = append(std::get<Tree::NodePtr>(node->children[i]));
                children[offset + i] = childIndex;
            }
        }
        return index;
    }
    explicit Snapshot(const rbush_tree &tree) { append(tree.tree.toJSON()); }
};
rbush_status results(const std::vector<Tree::ItemPtr> &values, rbush_item *output, size_t capacity, size_t *count) {
    require(count != nullptr, "out_count is required");
    *count = values.size();
    if (!output && capacity == 0) return RBUSH_OK;
    require(output != nullptr, "output buffer is required");
    if (capacity < values.size()) throw SmallBuffer("result buffer too small");
    for (size_t i = 0; i < values.size(); ++i) output[i] = item(values[i]);
    return RBUSH_OK;
}
}
extern "C" {
const char *rbush_last_error(void) { return lastError.c_str(); }
rbush_status rbush_create(int32_t maximum, rbush_tree **output) {
    return guard([&] { require(output, "out_tree is required"); *output = nullptr; *output = new rbush_tree(maximum); });
}
void rbush_destroy(rbush_tree *tree) { delete tree; }
rbush_status rbush_set_comparators(rbush_tree *tree, rbush_compare_fn x, rbush_compare_fn y, void *context) {
    return guard([&] {
        require(tree, "tree is required");
        tree->tree.compareX = x; tree->tree.compareY = y; tree->tree.compareContext = context;
    });
}
rbush_status rbush_get_comparators(const rbush_tree *tree, rbush_compare_fn *x, rbush_compare_fn *y, void **context) {
    return guard([&] { require(tree && x && y && context, "tree and outputs are required"); *x = tree->tree.compareX; *y = tree->tree.compareY; *context = tree->tree.compareContext; });
}
rbush_status rbush_get_capacity(const rbush_tree *tree, int32_t *maximum, int32_t *minimum) {
    return guard([&] { require(tree && maximum && minimum, "tree and outputs are required"); *maximum = tree->tree.maxEntries(); *minimum = tree->tree.minEntries(); });
}
rbush_status rbush_clear(rbush_tree *tree) {
    return guard([&] { require(tree, "tree is required"); tree->tree.clear(); tree->identities.clear(); });
}
rbush_status rbush_insert(rbush_tree *tree, rbush_item value) {
    return guard([&] { require(tree, "tree is required"); tree->tree.insert(project(*tree, value)); });
}
rbush_status rbush_load(rbush_tree *tree, const rbush_item *values, size_t count) {
    return guard([&] {
        require(tree && (values || count == 0), "tree and item buffer are required");
        std::vector<Tree::ItemPtr> projected; projected.reserve(count);
        for (size_t i = 0; i < count; ++i) projected.push_back(project(*tree, values[i]));
        tree->tree.load(projected);
    });
}
rbush_status rbush_remove_ex(rbush_tree *tree, rbush_item value, rbush_equals_fn equals, void *context,
                             int32_t *removed, uint64_t *removed_identity) {
    return guard([&] {
        require(tree, "tree is required");
        if (removed) *removed = 0;
        if (removed_identity) *removed_identity = 0;
        Tree::ItemPtr requested;
        const auto existing = tree->identities.find(value.identity);
        if (existing != tree->identities.end()) requested = existing->second.lock();
        if (!requested) {
            requested = std::make_shared<rbush::Item>();
            requested->minX = value.min_x; requested->minY = value.min_y;
            requested->maxX = value.max_x; requested->maxY = value.max_y; requested->id = value.identity;
        } else {
            // JS reads the requested object's current bbox, including caller mutations.
            requested->minX = value.min_x; requested->minY = value.min_y;
            requested->maxX = value.max_x; requested->maxY = value.max_y;
        }
        uint64_t matched = 0; bool found = false;
        tree->tree.remove(requested, [&](const Tree::ItemPtr &lhs, const Tree::ItemPtr &rhs) {
            const bool equal = equals ? equals(lhs->id, rhs->id, context) != 0 : lhs == rhs;
            if (equal) { found = true; matched = rhs->id; }
            return equal;
        });
        if (removed) *removed = found ? 1 : 0;
        if (removed_identity && found) *removed_identity = matched;
        // Drop our temporary ownership before testing whether the last tree
        // occurrence released the item; weak map entries must not accumulate.
        requested.reset();
        if (found) {
            const auto entry = tree->identities.find(matched);
            if (entry != tree->identities.end() && entry->second.expired()) tree->identities.erase(entry);
        }
    });
}
rbush_status rbush_remove(rbush_tree *tree, rbush_item value, rbush_equals_fn equals, void *context) {
    return rbush_remove_ex(tree, value, equals, context, nullptr, nullptr);
}
rbush_status rbush_all_result(const rbush_tree *tree, rbush_result **output) {
    return guard([&] {
        require(tree && output, "tree and out_result are required"); *output = nullptr;
        auto result = std::make_unique<rbush_result>();
        const auto values = tree->tree.all(); result->items.reserve(values.size());
        for (const auto &value : values) result->items.push_back(item(value));
        *output = result.release();
    });
}
rbush_status rbush_search_result(const rbush_tree *tree, rbush_box bounds, rbush_result **output) {
    return guard([&] {
        require(tree && output, "tree and out_result are required"); *output = nullptr;
        auto result = std::make_unique<rbush_result>();
        const auto values = tree->tree.search(box(bounds)); result->items.reserve(values.size());
        for (const auto &value : values) result->items.push_back(item(value));
        *output = result.release();
    });
}
size_t rbush_result_count(const rbush_result *result) { return result ? result->items.size() : 0; }
const rbush_item *rbush_result_data(const rbush_result *result) { return result ? result->items.data() : nullptr; }
void rbush_result_destroy(rbush_result *result) { delete result; }
rbush_status rbush_all(const rbush_tree *tree, rbush_item *output, size_t capacity, size_t *count) {
    rbush_status status = RBUSH_OK;
    const auto error = guard([&] { require(tree, "tree is required"); status = results(tree->tree.all(), output, capacity, count); });
    return error == RBUSH_OK ? status : error;
}
rbush_status rbush_search(const rbush_tree *tree, rbush_box bounds, rbush_item *output, size_t capacity, size_t *count) {
    rbush_status status = RBUSH_OK;
    const auto error = guard([&] { require(tree, "tree is required"); status = results(tree->tree.search(box(bounds)), output, capacity, count); });
    return error == RBUSH_OK ? status : error;
}
rbush_status rbush_collides(const rbush_tree *tree, rbush_box bounds, int32_t *output) {
    return guard([&] { require(tree && output, "tree and out_collides are required"); *output = tree->tree.collides(box(bounds)) ? 1 : 0; });
}
rbush_status rbush_export_counts(const rbush_tree *tree, size_t *nodes, size_t *items, size_t *children) {
    return guard([&] { require(tree && nodes && items && children, "tree and counts are required"); const Snapshot snapshot(*tree);
        *nodes = snapshot.nodes.size(); *items = snapshot.items.size(); *children = snapshot.children.size(); });
}
rbush_status rbush_to_json(const rbush_tree *tree, rbush_node *nodes, size_t node_capacity,
                           rbush_item *items, size_t item_capacity, uint64_t *children, size_t child_capacity) {
    rbush_status status = RBUSH_OK;
    const auto error = guard([&] {
        require(tree, "tree is required"); const Snapshot snapshot(*tree);
        if (node_capacity < snapshot.nodes.size() || item_capacity < snapshot.items.size() || child_capacity < snapshot.children.size()) {
            throw SmallBuffer("snapshot buffers too small");
        }
        require(nodes && (items || snapshot.items.empty()) && (children || snapshot.children.empty()), "snapshot buffers are required");
        std::copy(snapshot.nodes.begin(), snapshot.nodes.end(), nodes);
        std::copy(snapshot.items.begin(), snapshot.items.end(), items);
        std::copy(snapshot.children.begin(), snapshot.children.end(), children);
    });
    return error == RBUSH_OK ? status : error;
}
rbush_status rbush_from_json(rbush_tree *tree, const rbush_node *nodes, size_t node_count,
                             const rbush_item *items, size_t item_count, const uint64_t *children, size_t child_count) {
    return guard([&] {
        require(tree && nodes && node_count && (items || !item_count) && (children || !child_count), "snapshot buffers are required");
        rbush_tree imported(tree->tree.maxEntries());
        std::vector<uint8_t> seen(node_count, 0);
        std::function<Tree::NodePtr(size_t)> read = [&](size_t index) -> Tree::NodePtr {
            require(index < node_count && !seen[index], "node index out of range, repeated, or cyclic");
            seen[index] = 1;
            const auto &source = nodes[index];
            require(source.leaf == 0 || source.leaf == 1, "leaf must be 0 or 1");
            require(source.leaf ? source.height == 1 : source.height > 1 && source.child_count > 0,
                    "leaf height must be 1; branches require height > 1 and children");
            const auto limit = source.leaf ? item_count : child_count;
            require(source.child_offset <= limit && source.child_count <= limit - source.child_offset, "child range out of bounds");
            auto node = std::make_shared<Tree::Node>();
            node->minX = source.min_x; node->minY = source.min_y; node->maxX = source.max_x; node->maxY = source.max_y;
            node->height = source.height; node->leaf = source.leaf != 0;
            node->children.reserve(static_cast<size_t>(source.child_count));
            for (uint64_t i = 0; i < source.child_count; ++i) {
                const auto offset = source.child_offset + i;
                if (node->leaf) node->children.emplace_back(project(imported, items[offset]));
                else {
                    auto child = read(static_cast<size_t>(children[offset]));
                    require(child->height == source.height - 1, "child height must equal parent height minus 1");
                    node->children.emplace_back(std::move(child));
                }
            }
            return node;
        };
        auto root = read(0);
        tree->tree.fromJSON(std::move(root));
        tree->identities = std::move(imported.identities);
    });
}
rbush_status rbush_root_bounds(const rbush_tree *tree, rbush_box *bounds, int32_t *empty) {
    return guard([&] {
        require(tree && bounds && empty, "tree and root outputs are required");
        const auto &root = tree->tree.data;
        *bounds = {root->minX, root->minY, root->maxX, root->maxY};
        *empty = root->children.empty() ? 1 : 0;
    });
}
rbush_status rbush_clone(const rbush_tree *tree, rbush_tree **output) {
    return guard([&] {
        require(tree && output, "tree and clone output are required"); *output = nullptr;
        auto copy = std::make_unique<rbush_tree>(tree->tree.maxEntries());
        // C ABI items are canonical by identity. The final weak map itself is
        // the duplicate cache; cloned nodes keep every copied item alive.
        // Reserve once to avoid rehashing and a second N-entry ownership map.
        copy->identities.reserve(tree->identities.size());
        std::unordered_map<const Tree::Node *, Tree::NodePtr> copiedNodes;
        std::function<Tree::NodePtr(const Tree::NodePtr &)> duplicate = [&](const Tree::NodePtr &source) {
            auto existing = copiedNodes.find(source.get());
            if (existing != copiedNodes.end()) return existing->second;
            auto node = std::make_shared<Tree::Node>();
            static_cast<rbush::Box &>(*node) = static_cast<const rbush::Box &>(*source);
            node->height = source->height; node->leaf = source->leaf;
            copiedNodes.emplace(source.get(), node);
            node->children.reserve(source->children.size());
            for (const auto &child : source->children) {
                if (source->leaf) {
                    const auto &original = std::get<Tree::ItemPtr>(child);
                    auto &cached = copy->identities[original->id];
                    auto value = cached.lock();
                    if (!value) {
                        value = std::make_shared<rbush::Item>(*original);
                        cached = value;
                    }
                    node->children.emplace_back(std::move(value));
                } else node->children.emplace_back(duplicate(std::get<Tree::NodePtr>(child)));
            }
            return node;
        };
        copy->tree.fromJSON(duplicate(tree->tree.data));
        copy->tree.compareX = tree->tree.compareX; copy->tree.compareY = tree->tree.compareY;
        copy->tree.compareContext = tree->tree.compareContext;
        *output = copy.release();
    });
}
rbush_status rbush_any(const rbush_tree *tree, rbush_box bounds, rbush_predicate_fn predicate,
                      void *context, int32_t *found, size_t *node_visits, size_t *item_tests) {
    return guard([&] {
        require(tree && predicate && found, "tree, predicate and found output are required");
        *found = 0; size_t visits = 0, tests = 0;
        const auto query = box(bounds);
        std::vector<Tree::NodePtr> pending;
        if (rbush::detail::intersects(query, *tree->tree.data)) pending.push_back(tree->tree.data);
        while (!pending.empty() && !*found) {
            const auto node = pending.back(); pending.pop_back(); ++visits;
            if (node->leaf) {
                for (const auto &child : node->children) {
                    const auto &candidate = std::get<Tree::ItemPtr>(child); ++tests;
                    if (rbush::detail::intersects(query, *candidate) && predicate(candidate->id, context)) {
                        *found = 1; break;
                    }
                }
            } else {
                for (const auto &child : node->children) {
                    const auto &next = std::get<Tree::NodePtr>(child);
                    if (rbush::detail::intersects(query, *next)) pending.push_back(next);
                }
            }
        }
        if (node_visits) *node_visits = visits;
        if (item_tests) *item_tests = tests;
    });
}
rbush_status rbush_regions(const rbush_tree *tree, size_t max_regions, size_t max_visits,
                          double min_ratio, rbush_box *output, size_t capacity, size_t *count, size_t *node_visits) {
    return rbush_regions_ex(tree, max_regions, max_visits, min_ratio, output, capacity, count, node_visits, nullptr);
}
rbush_status rbush_regions_ex(const rbush_tree *tree, size_t max_regions, size_t max_visits,
                             double min_ratio, rbush_box *output, size_t capacity, size_t *count,
                             size_t *node_visits, size_t *entry_reads) {
    return guard([&] {
        require(tree && count, "tree and region count are required");
        require(std::isfinite(min_ratio) && min_ratio >= 0, "min_span_ratio must be finite and nonnegative");
        *count = 0; if (node_visits) *node_visits = 0; if (entry_reads) *entry_reads = 0;
        if (!max_regions || !max_visits || tree->tree.data->children.empty()) return;
        struct Region {
            Tree::NodePtr node;
            rbush_box bounds;
            size_t children;
            double extent;
            bool leaf;
        };
        const auto extent = [](const rbush_box &bounds) {
            return std::max(bounds.max_x - bounds.min_x, bounds.max_y - bounds.min_y);
        };
        const auto nodeRegion = [&](const Tree::NodePtr &node) {
            const rbush_box bounds{node->minX, node->minY, node->maxX, node->maxY};
            return Region{node, bounds, node->children.size(), extent(bounds), node->leaf};
        };
        // Node visits and leaf-entry reads have separate equal budgets. A leaf
        // box may bridge clouds, but even a very large leaf cannot trigger an
        // unbounded scan. The temporary frontier is bounded by twice the budget.
        std::vector<Region> frontier{nodeRegion(tree->tree.data)};
        size_t visits = 1, itemReads = 0;
        const double threshold = frontier.front().extent * min_ratio;
        while (true) {
            size_t selected = frontier.size(); double largest = -1;
            for (size_t i = 0; i < frontier.size(); ++i) {
                const auto &entry = frontier[i];
                if (!entry.node || entry.children == 0) continue;
                const size_t remaining = entry.leaf ? max_visits - itemReads : max_visits - visits;
                if (entry.children > remaining) continue;
                if (entry.extent > threshold && entry.extent > largest) { selected = i; largest = entry.extent; }
            }
            if (selected == frontier.size()) break;
            const auto node = frontier[selected].node;
            std::vector<Region> children; children.reserve(node->children.size());
            for (const auto &child : node->children) {
                if (node->leaf) {
                    const auto &value = std::get<Tree::ItemPtr>(child);
                    const rbush_box bounds{value->minX, value->minY, value->maxX, value->maxY};
                    children.push_back({nullptr, bounds, 0, extent(bounds), false});
                } else children.push_back(nodeRegion(std::get<Tree::NodePtr>(child)));
            }
            if (node->leaf) itemReads += children.size();
            else visits += children.size();
            frontier.erase(frontier.begin() + static_cast<std::ptrdiff_t>(selected));
            frontier.insert(frontier.begin() + static_cast<std::ptrdiff_t>(selected), children.begin(), children.end());
        }
        // Refinement is limited by reads, not by the final drawing budget.
        // Otherwise a wide bridge could never expand when it has many children.
        std::vector<rbush_box> regions; regions.reserve(frontier.size());
        for (const auto &entry : frontier) regions.push_back(entry.bounds);
        const auto unite = [](const rbush_box &a, const rbush_box &b) {
            return rbush_box{std::min(a.min_x, b.min_x), std::min(a.min_y, b.min_y),
                             std::max(a.max_x, b.max_x), std::max(a.max_y, b.max_y)};
        };
        const auto merge = [&](size_t a, size_t b) {
            regions[a] = unite(regions[a], regions[b]);
            regions.erase(regions.begin() + static_cast<std::ptrdiff_t>(b));
        };
        // Coalesce only within one root-relative granularity box. This avoids
        // transitive chains that could join two distant clouds across a gap.
        bool coalesced = true;
        while (coalesced) {
            coalesced = false;
            for (size_t i = 0; i < regions.size() && !coalesced; ++i) {
                for (size_t j = i + 1; j < regions.size(); ++j) {
                    if (extent(unite(regions[i], regions[j])) <= threshold) {
                        merge(i, j); coalesced = true; break;
                    }
                }
            }
        }
        const auto area = [](const rbush_box &bounds) {
            return (bounds.max_x - bounds.min_x) * (bounds.max_y - bounds.min_y);
        };
        while (regions.size() > max_regions) {
            size_t first = 0, second = 1;
            double closest = std::numeric_limits<double>::infinity();
            double smallestGrowth = std::numeric_limits<double>::infinity();
            for (size_t i = 0; i < regions.size(); ++i) {
                for (size_t j = i + 1; j < regions.size(); ++j) {
                    const auto &a = regions[i]; const auto &b = regions[j];
                    const double dx = std::max(0.0, std::max(a.min_x, b.min_x) - std::min(a.max_x, b.max_x));
                    const double dy = std::max(0.0, std::max(a.min_y, b.min_y) - std::min(a.max_y, b.max_y));
                    const double distance = dx * dx + dy * dy;
                    const double growth = area(unite(a, b)) - area(a) - area(b);
                    if (distance < closest || (distance == closest && growth < smallestGrowth)) {
                        first = i; second = j; closest = distance; smallestGrowth = growth;
                    }
                }
            }
            merge(first, second);
        }
        *count = regions.size(); if (node_visits) *node_visits = visits; if (entry_reads) *entry_reads = itemReads;
        if (capacity < regions.size()) throw SmallBuffer("region buffer too small");
        require(output, "region buffer is required");
        std::copy(regions.begin(), regions.end(), output);
    });
}

}
