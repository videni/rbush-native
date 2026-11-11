// Adapter bookkeeping is deliberately tested in its own translation unit so
// the public C ABI needs no test-only introspection functions.
#ifndef RBUSH_C_IMPLEMENTATION
#define RBUSH_C_IMPLEMENTATION "../bindings/c/src/rbush_c.cpp"
#endif
#include RBUSH_C_IMPLEMENTATION
#include <iostream>

static void expect(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    rbush_tree *tree = nullptr;
    expect(rbush_create(9, &tree) == RBUSH_OK, "create");
    std::unique_ptr<rbush_tree, decltype(&rbush_destroy)> owner(tree, rbush_destroy);
    const rbush_item item{0, 0, 1, 1, 1};
    expect(rbush_insert(tree, item) == RBUSH_OK, "insert first");
    expect(rbush_insert(tree, item) == RBUSH_OK, "insert duplicate");
    expect(tree->identities.size() == 1, "duplicates share one identity");
    expect(rbush_remove(tree, item, nullptr, nullptr) == RBUSH_OK, "remove duplicate");
    expect(tree->identities.size() == 1 && !tree->identities.at(1).expired(), "remaining occurrence owns item");
    expect(rbush_remove(tree, item, nullptr, nullptr) == RBUSH_OK, "remove last");
    expect(tree->identities.empty(), "last removal must erase expired identity bookkeeping");
    for (uint64_t id = 2; id < 1002; ++id) {
        const rbush_item edited{0, 0, 1, 1, id};
        expect(rbush_insert(tree, edited) == RBUSH_OK, "repeated insert");
        expect(rbush_remove(tree, edited, nullptr, nullptr) == RBUSH_OK, "repeated remove");
    }
    expect(tree->identities.empty(), "monotonic IDs must not accumulate weak map entries");
    expect(rbush_insert(tree, item) == RBUSH_OK, "insert before custom remove");
    int32_t removed = 0; uint64_t removedID = 0;
    expect(rbush_remove_ex(tree, rbush_item{0, 0, 1, 1, 9999},
        [](uint64_t lhs, uint64_t rhs, void *) { return lhs == 9999 && rhs == 1 ? 1 : 0; },
        nullptr, &removed, &removedID) == RBUSH_OK, "custom equality");
    expect(removed == 1 && removedID == 1 && tree->identities.empty(), "custom remove cleanup");
    expect(rbush_insert(tree, item) == RBUSH_OK, "insert before clear");
    expect(rbush_clear(tree) == RBUSH_OK && tree->identities.empty(), "clear cleanup");
    // Invalid imported metadata must not expose impossible core traversal paths.
    expect(rbush_insert(tree, item) == RBUSH_OK, "seed import validation");
    auto rejects = [&](const std::vector<rbush_node> &nodes, const std::vector<uint64_t> &indices) {
        expect(rbush_from_json(tree, nodes.data(), nodes.size(), nullptr, 0,
            indices.data(), indices.size()) == RBUSH_INVALID_ARGUMENT, "reject invalid node structure");
        size_t count = 0;
        expect(rbush_all(tree, nullptr, 0, &count) == RBUSH_OK && count == 1, "invalid import leaves prior tree unchanged");
    };
    rejects({{0, 0, 1, 1, 0, 0, 2, 1}}, {}); // leaf height must be one
    rejects({{0, 0, 1, 1, 0, 0, 2, 0}}, {}); // branches must have children
    rejects({{0, 0, 1, 1, 0, 1, 1, 0}, {0, 0, 1, 1, 0, 0, 1, 1}}, {1});
    rejects({{0, 0, 1, 1, 0, 1, 3, 0}, {0, 0, 1, 1, 0, 0, 1, 1}}, {1});
    expect(rbush_insert(tree, rbush_item{2, 2, 3, 3, 2}) == RBUSH_OK, "insert remains safe after invalid imports");

    expect(rbush_clear(tree) == RBUSH_OK, "clear before comparator test");
    int comparisons = 0;
    const rbush_compare_fn reverse = [](uint64_t lhs, uint64_t rhs, void *raw) {
        ++*static_cast<int *>(raw);
        return static_cast<double>(rhs) - static_cast<double>(lhs);
    };
    expect(rbush_set_comparators(tree, reverse, reverse, &comparisons) == RBUSH_OK, "set comparator hooks");
    std::vector<rbush_item> batch;
    for (uint64_t id = 0; id < 100; ++id) batch.push_back({double(id), double(id), double(id + 1), double(id + 1), id});
    expect(rbush_load(tree, batch.data(), batch.size()) == RBUSH_OK && comparisons > 0, "bulk load calls comparator hooks");
    rbush_compare_fn x = nullptr, y = nullptr; void *context = nullptr;
    expect(rbush_get_comparators(tree, &x, &y, &context) == RBUSH_OK && x == reverse && y == reverse && context == &comparisons, "get hooks");
    const rbush_node empty{INFINITY, INFINITY, -INFINITY, -INFINITY, 0, 0, 1, 1};
    expect(rbush_from_json(tree, &empty, 1, nullptr, 0, nullptr, 0) == RBUSH_OK, "import empty tree");
    comparisons = 0;
    expect(rbush_load(tree, batch.data(), batch.size()) == RBUSH_OK && comparisons > 0, "from_json retains comparator hooks");
    expect(rbush_clear(tree) == RBUSH_OK, "clear retains hook config");
    comparisons = 0;
    expect(rbush_load(tree, batch.data(), batch.size()) == RBUSH_OK && comparisons > 0, "clear retains comparator hooks");
    expect(rbush_set_comparators(tree, nullptr, nullptr, nullptr) == RBUSH_OK, "restore original comparators");
    expect(rbush_clear(tree) == RBUSH_OK, "clear after resetting hooks");
    comparisons = 0;
    expect(rbush_load(tree, batch.data(), batch.size()) == RBUSH_OK && comparisons == 0, "null hooks bypass callback");
    std::cout << "C adapter lifetime/bookkeeping tests passed\n";
}
