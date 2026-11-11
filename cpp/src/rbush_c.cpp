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
}
