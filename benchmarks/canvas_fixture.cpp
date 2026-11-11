#include "rbush.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
using Tree = rbush::RBush<>;
using Clock = std::chrono::steady_clock;
using Owners = std::unordered_map<std::uint64_t, Tree::ItemPtr>;
struct Query {
    rbush::Box bbox;
    std::vector<std::uint64_t> expected;
};
struct Group {
    double radius = 0;
    double excludedFraction = 0;
    std::unordered_set<std::uint64_t> excluded;
    std::vector<Query> queries;
    std::uint64_t expectedTotal = 0;
};
struct Fixture {
    std::uint64_t count = 0;
    std::vector<Tree::ItemPtr> items;
    Owners owners;
    std::vector<Group> groups;
};
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void readBox(std::istream& input, rbush::Box& box) {
    require(bool(input >> box.minX >> box.minY >> box.maxX >> box.maxY), "invalid bounding box");
}
Fixture readFixture(const std::string& filename) {
    std::ifstream input(filename);
    require(bool(input), "cannot open fixture: " + filename);
    Fixture fixture;
    std::size_t groupCount = 0;
    require(bool(input >> fixture.count >> groupCount), "invalid fixture header");
    require(fixture.count > 0 && groupCount > 0, "fixture must contain items and query groups");
    fixture.items.reserve(static_cast<std::size_t>(fixture.count + 1));
    fixture.owners.reserve(static_cast<std::size_t>(fixture.count + 1));
    for (std::uint64_t i = 0; i <= fixture.count; ++i) {
        auto item = std::make_shared<rbush::Item>();
        require(bool(input >> item->id), "invalid item identity");
        readBox(input, *item);
        require(item->id <= fixture.count, "item identity outside fixture range");
        require(fixture.owners.emplace(item->id, item).second, "duplicate item identity");
        if (i == fixture.count) require(item->id == fixture.count, "final item must be Drawing root");
        fixture.items.push_back(std::move(item));
    }
    fixture.groups.reserve(groupCount);
    for (std::size_t i = 0; i < groupCount; ++i) {
        Group group;
        std::size_t excludedCount = 0, queryCount = 0;
        require(bool(input >> group.radius >> group.excludedFraction >> excludedCount >> queryCount), "invalid query group header");
        require(queryCount == 240, "canvas protocol requires exactly 240 queries per group");
        group.excluded.reserve(excludedCount);
        for (std::size_t j = 0; j < excludedCount; ++j) {
            std::uint64_t id = 0;
            require(bool(input >> id), "invalid excluded identity");
            require(id < fixture.count && group.excluded.insert(id).second, "invalid or duplicate excluded identity");
        }
        group.queries.reserve(queryCount);
        for (std::size_t j = 0; j < queryCount; ++j) {
            Query query;
            readBox(input, query.bbox);
            std::size_t expectedCount = 0;
            require(bool(input >> expectedCount), "invalid expected candidate count");
            query.expected.resize(expectedCount);
            for (auto& id : query.expected) require(bool(input >> id), "invalid expected candidate identity");
            require(std::is_sorted(query.expected.begin(), query.expected.end()), "oracle identities must be sorted");
            group.expectedTotal += expectedCount;
            group.queries.push_back(std::move(query));
        }
        fixture.groups.push_back(std::move(group));
    }
    std::string trailing;
    require(!(input >> trailing), "unexpected data after fixture groups");
    return fixture;
}
bool intersects(const rbush::Box& a, const rbush::Box& b) {
    return b.minX <= a.maxX && b.minY <= a.maxY && b.maxX >= a.minX && b.maxY >= a.minY;
}
// Same ownership/bounds/exclusion post-filter as the other canvas candidates.
// Both the tree result and the filtered result are allocated for each real query.
std::vector<std::uint64_t> filtered(const Tree& tree, const Owners& owners,
                                  std::uint64_t count, const Group& group, const Query& query) {
    const auto candidates = tree.search(query.bbox);
    std::vector<std::uint64_t> result;
    for (const auto& candidate : candidates) {
        const auto found = owners.find(candidate->id);
        if (found == owners.end()) continue;
        const auto& owner = found->second;
        if (owner->id < count && intersects(*owner, query.bbox) && group.excluded.count(owner->id) == 0) {
            result.push_back(owner->id);
        }
    }
    return result;
}
std::uint64_t runLoop(const Tree& tree, const Fixture& fixture, const Group& group) {
    std::uint64_t total = 0;
    for (const auto& query : group.queries) total += filtered(tree, fixture.owners, fixture.count, group, query).size();
    return total;
}
void benchmark(const char* algorithm, const Tree& tree, const Fixture& fixture, const Group& group) {
    for (std::size_t i = 0; i < group.queries.size(); ++i) {
        auto actual = filtered(tree, fixture.owners, fixture.count, group, group.queries[i]);
        std::sort(actual.begin(), actual.end());
        require(actual == group.queries[i].expected, std::string(algorithm) + " oracle mismatch at query " + std::to_string(i));
    }
    for (int i = 0; i < 2; ++i) {
        require(runLoop(tree, fixture, group) == group.expectedTotal, "warmup candidate total mismatch");
    }
    std::vector<double> samples;
    samples.reserve(9);
    for (int i = 0; i < 9; ++i) {
        const auto start = Clock::now();
        const auto total = runLoop(tree, fixture, group);
        const auto end = Clock::now();
        require(total == group.expectedTotal, "measured candidate total mismatch");
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(samples.begin(), samples.end());
    std::cout << std::setprecision(12)
        << "{\"algorithm\":\"" << algorithm << "\",\"N\":" << fixture.count
        << ",\"world_radius\":" << group.radius << ",\"excluded_fraction\":" << group.excludedFraction
        << ",\"query_segments\":" << group.queries.size() << ",\"total_candidates\":" << group.expectedTotal
        << ",\"loop_p50_ms\":" << samples[4] << ",\"loop_p95_ms\":" << samples[8]
        << ",\"samples\":" << samples.size() << ",\"oracle_verified\":true}\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: rbush_canvas_fixture <shared-canvas-fixture.txt>");
        const auto fixture = readFixture(argv[1]);
        Tree inserted;
        for (const auto& item : fixture.items) inserted.insert(item);
        Tree loaded;
        loaded.load(fixture.items);
        for (const auto& group : fixture.groups) {
            benchmark("insert_loop", inserted, fixture, group);
            benchmark("bulk_load", loaded, fixture, group);
        }
    } catch (const std::exception& error) {
        std::cerr << "canvas fixture benchmark: " << error.what() << '\n';
        return 1;
    }
}
