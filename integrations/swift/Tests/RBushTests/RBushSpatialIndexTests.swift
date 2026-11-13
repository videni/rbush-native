import XCTest
import CRBush
@testable import RBush

final class RBushSpatialIndexTests: XCTestCase {
    func box(_ x: Double, _ y: Double = 0, _ size: Double = 1) -> BoundingBox {
        .init(minX: x, minY: y, maxX: x + size, maxY: y + size)
    }
    func testCompassSummarySeparatesDenseCloudsWithoutDroppingCoverage() throws {
        for side in [3, 16] {
            let entries: [(Int, BoundingBox)] = [0.0, 10000.0].enumerated().flatMap { cloud, origin in
                (0..<(side * side)).map { i in
                    (cloud * side * side + i, box(origin + Double(i % side) * 10, Double(i / side) * 10, 8))
                }
            }
            var index = RBushSpatialIndex<Int>()
            try index.load(entries)
            let regions = try index.regions(maxRegions: 12, maxNodeVisits: 96, minSpanRatio: 0.02)
            XCTAssertGreaterThan(regions.count, 1)
            XCTAssertLessThanOrEqual(regions.count, 12)
            XCTAssertFalse(regions.contains { $0.minX <= 5000 && $0.maxX >= 5000 && $0.minY <= 50 && $0.maxY >= 50 })
            XCTAssertTrue(regions.allSatisfy { $0.maxX < 5000 || $0.minX > 5000 }, "side=\(side), regions=\(regions)")
            for (_, bounds) in entries {
                XCTAssertTrue(regions.contains { $0.minX <= bounds.minX && $0.minY <= bounds.minY && $0.maxX >= bounds.maxX && $0.maxY >= bounds.maxY })
            }
            let root = try XCTUnwrap(index.bounds)
            XCTAssertEqual(regions.map(\.minX).min(), root.minX)
            XCTAssertEqual(regions.map(\.maxX).max(), root.maxX)
            XCTAssertEqual(regions.map(\.minY).min(), root.minY)
            XCTAssertEqual(regions.map(\.maxY).max(), root.maxY)
        }
    }
    func testCompassSummaryLargeLeafCannotExceedSeparateEntryBudget() throws {
        var tree: OpaquePointer?
        XCTAssertEqual(rbush_create(1000, &tree), RBUSH_OK)
        defer { rbush_destroy(tree) }
        let items = (0..<512).map { rbush_item(min_x: Double($0), min_y: 0, max_x: Double($0 + 1), max_y: 1, identity: UInt64($0)) }
        items.withUnsafeBufferPointer { XCTAssertEqual(rbush_load(tree, $0.baseAddress, $0.count), RBUSH_OK) }
        var boxes = [rbush_box](repeating: .init(), count: 12), count = 0, nodes = 0, entries = 0
        boxes.withUnsafeMutableBufferPointer {
            XCTAssertEqual(rbush_regions_ex(tree, 12, 96, 0.02, $0.baseAddress, $0.count, &count, &nodes, &entries), RBUSH_OK)
        }
        XCTAssertEqual(nodes, 1)
        XCTAssertEqual(entries, 0, "large leaves must not trigger an unbounded item scan")
        XCTAssertEqual(count, 1)
        XCTAssertEqual(boxes[0].min_x, 0)
        XCTAssertEqual(boxes[0].max_x, 512)
    }
    func testNativeClonePreservesDuplicateIdentitiesWithoutAliasingItems() throws {
        var original: OpaquePointer?, clone: OpaquePointer?
        XCTAssertEqual(rbush_create(4, &original), RBUSH_OK)
        defer { rbush_destroy(original); rbush_destroy(clone) }
        var root = rbush_box(), empty: Int32 = 0
        XCTAssertEqual(rbush_root_bounds(original, &root, &empty), RBUSH_OK)
        XCTAssertEqual(empty, 1)
        let point = rbush_item(min_x: 0, min_y: 0, max_x: 0, max_y: 0, identity: 7)
        XCTAssertEqual(rbush_insert(original, point), RBUSH_OK)
        XCTAssertEqual(rbush_insert(original, point), RBUSH_OK)
        XCTAssertEqual(rbush_root_bounds(original, &root, &empty), RBUSH_OK)
        XCTAssertEqual(empty, 0)
        XCTAssertEqual(root.max_x, 0)
        XCTAssertEqual(rbush_clone(original, &clone), RBUSH_OK)
        // Same identity changes original mutable native geometry only.
        XCTAssertEqual(rbush_insert(original, .init(min_x: 20, min_y: 20, max_x: 21, max_y: 21, identity: 7)), RBUSH_OK)
        var count = 0
        let origin = rbush_box(min_x: 0, min_y: 0, max_x: 0, max_y: 0)
        XCTAssertEqual(rbush_search(clone, origin, nil, 0, &count), RBUSH_OK)
        XCTAssertEqual(count, 2)
        XCTAssertEqual(rbush_search(original, origin, nil, 0, &count), RBUSH_OK)
        XCTAssertEqual(count, 0)
        XCTAssertEqual(rbush_remove(clone, point, nil, nil), RBUSH_OK)
        XCTAssertEqual(rbush_all(clone, nil, 0, &count), RBUSH_OK)
        XCTAssertEqual(count, 1)
        XCTAssertEqual(rbush_all(original, nil, 0, &count), RBUSH_OK)
        XCTAssertEqual(count, 3)
    }
    func testNativeEarlyStopAndRegionWorkCountersRespectBudgets() throws {
        var tree: OpaquePointer?
        XCTAssertEqual(rbush_create(4, &tree), RBUSH_OK)
        defer { rbush_destroy(tree) }
        let items = (0..<1000).map { rbush_item(min_x: Double($0), min_y: 0, max_x: Double($0 + 1), max_y: 1, identity: UInt64($0)) }
        items.withUnsafeBufferPointer { XCTAssertEqual(rbush_load(tree, $0.baseAddress, $0.count), RBUSH_OK) }
        var calls = 0, visits = 0, tests = 0
        var found: Int32 = 0
        let entire = rbush_box(min_x: 0, min_y: 0, max_x: 1001, max_y: 1)
        XCTAssertEqual(rbush_any(tree, entire, { _, context in
            guard let context else { return 0 }
            context.assumingMemoryBound(to: Int.self).pointee += 1
            return 1
        }, &calls, &found, &visits, &tests), RBUSH_OK)
        XCTAssertEqual(found, 1)
        XCTAssertEqual(calls, 1)
        XCTAssertEqual(tests, 1, "must not enumerate all matching items before invoking predicate")
        XCTAssertLessThan(visits, 10)
        for budget in [1, 4, 16, 64] {
            var boxes = [rbush_box](repeating: .init(), count: 16), count = 0, entries = 0
            boxes.withUnsafeMutableBufferPointer {
                XCTAssertEqual(rbush_regions_ex(tree, 16, budget, 0, $0.baseAddress, $0.count, &count, &visits, &entries), RBUSH_OK)
            }
            XCTAssertLessThanOrEqual(visits, budget)
            XCTAssertLessThanOrEqual(entries, budget)
            XCTAssertLessThanOrEqual(count, 16)
            XCTAssertGreaterThan(count, 0)
            if budget == 1 { XCTAssertEqual(count, 1) }
            if budget == 64 { XCTAssertGreaterThan(count, 1) }
        }
    }
    func testValueUpsertCopyRemovalAndExtremeBounds() throws {
        var index = RBushSpatialIndex<Int>()
        XCTAssertNil(index.bounds)
        XCTAssertTrue(try index.insert(1, bounds: box(0, 0, 0)))
        XCTAssertTrue(try index.insert(2, bounds: box(100)))
        XCTAssertEqual(index.count, 2)
        XCTAssertEqual(index.bounds, .init(minX: 0, minY: 0, maxX: 101, maxY: 1))
        let original = index
        XCTAssertFalse(try index.insert(2, bounds: box(100)))
        XCTAssertTrue(try index.insert(2, bounds: box(5)))
        XCTAssertEqual(try original.candidates(in: box(100)), [2])
        XCTAssertEqual(try index.candidates(in: box(5)), [2])
        XCTAssertTrue(try index.candidates(in: box(100)).isEmpty)
        XCTAssertEqual(original.bounds?.maxX, 101)
        XCTAssertEqual(index.bounds?.maxX, 6)
        XCTAssertTrue(try index.removeAll([2, 2, 99]))
        XCTAssertEqual(index.bounds, box(0, 0, 0))
        XCTAssertFalse(try index.remove(99))
        index.reset()
        XCTAssertEqual(index.count, 0)
        XCTAssertNil(index.bounds)
        XCTAssertEqual(original.count, 2)
    }
    func testBulkLastWinsAndCopyIndependence() throws {
        var index = RBushSpatialIndex<Int>(maxEntries: 4)
        XCTAssertTrue(try index.load((0..<100).map { ($0, box(Double($0))) }))
        let original = index
        XCTAssertTrue(try index.load([(7, box(700)), (7, box(701)), (101, box(101))]))
        XCTAssertEqual(index.count, 101)
        XCTAssertEqual(try index.candidates(in: box(701)), [7])
        XCTAssertTrue(try original.candidates(in: box(701)).isEmpty)
        XCTAssertEqual(try original.candidates(in: box(7, 0, 0)), [6, 7])
        XCTAssertFalse(try index.load([(7, box(701))]))
        XCTAssertEqual(original.count, 100)
    }
    func testBatchMovesStoredKeysAndReusesReleasedIdentities() throws {
        var index = RBushSpatialIndex<String>(maxEntries: 4)
        XCTAssertTrue(try index.load((0..<40).map { ("k\($0)", box(Double($0) * 10)) }))
        // Stored key moved twice in one batch, a new key repeated, and an unchanged key.
        XCTAssertTrue(try index.load([("k3", box(900)), ("k3", box(950)), ("n", box(500)), ("n", box(600)), ("k5", box(50))]))
        XCTAssertEqual(index.count, 41)
        XCTAssertEqual(try index.candidates(in: box(950)), ["k3"])
        XCTAssertTrue(try index.candidates(in: box(900, 0, 0.5)).isEmpty)
        XCTAssertTrue(try index.candidates(in: box(30, 0, 0.5)).isEmpty)
        XCTAssertEqual(try index.candidates(in: box(600)), ["n"])
        XCTAssertTrue(try index.candidates(in: box(500, 0, 0.5)).isEmpty)
        XCTAssertFalse(try index.load([("k3", box(950)), ("n", box(600))]))
        XCTAssertTrue(try index.removeAll(["k3", "n", "k3", "missing"]))
        XCTAssertEqual(index.count, 39)
        XCTAssertTrue(try index.candidates(in: box(950)).isEmpty)
        // Released identities are reused without resurrecting the removed keys.
        XCTAssertTrue(try index.load([("fresh1", box(950)), ("fresh2", box(600))]))
        XCTAssertEqual(try index.candidates(in: box(950)), ["fresh1"])
        XCTAssertEqual(try index.candidates(in: box(600)), ["fresh2"])
        XCTAssertEqual(Set(try index.candidates(in: .init(minX: -1, minY: -1, maxX: 2000, maxY: 2))).count, 41)
    }
    func testSharedNoOpWriteLeavesBothCopiesIntact() throws {
        var index = RBushSpatialIndex<Int>()
        try index.load((0..<20).map { ($0, box(Double($0) * 3)) })
        let snapshot = index
        XCTAssertFalse(try index.load([(4, box(12)), (7, box(21))]))
        XCTAssertFalse(try index.removeAll([99]))
        XCTAssertTrue(try index.insert(4, bounds: box(400)))
        XCTAssertEqual(try snapshot.candidates(in: box(12, 0, 0.5)), [4])
        XCTAssertTrue(try snapshot.candidates(in: box(400)).isEmpty)
        XCTAssertEqual(try index.candidates(in: box(400)), [4])
    }
    func testNativeIdentityUpsertCountsChangesAndRemoves() {
        var tree: OpaquePointer?
        XCTAssertEqual(rbush_create(4, &tree), RBUSH_OK)
        defer { rbush_destroy(tree) }
        func item(_ identity: UInt64, _ x: Double) -> rbush_item {
            .init(min_x: x, min_y: 0, max_x: x + 1, max_y: 1, identity: identity)
        }
        var changed = 0, preview = 0, removed = 0
        let first = (1...30).map { item(UInt64($0), Double($0) * 10) }
        XCTAssertEqual(rbush_count_changes(tree, first, first.count, &preview), RBUSH_OK)
        XCTAssertEqual(preview, 30)
        XCTAssertEqual(rbush_upsert_many(tree, first, first.count, &changed), RBUSH_OK)
        XCTAssertEqual(changed, 30)
        let second = [item(3, 30), item(4, 700), item(4, 800), item(31, 5), item(31, 6)]
        XCTAssertEqual(rbush_count_changes(tree, second, second.count, &preview), RBUSH_OK)
        XCTAssertEqual(preview, 4)
        XCTAssertEqual(rbush_upsert_many(tree, second, second.count, &changed), RBUSH_OK)
        XCTAssertEqual(changed, 4)
        var count = 0
        XCTAssertEqual(rbush_all(tree, nil, 0, &count), RBUSH_OK)
        XCTAssertEqual(count, 31)
        XCTAssertEqual(rbush_search(tree, .init(min_x: 800, min_y: 0, max_x: 801, max_y: 1), nil, 0, &count), RBUSH_OK)
        XCTAssertEqual(count, 1)
        XCTAssertEqual(rbush_search(tree, .init(min_x: 700, min_y: 0, max_x: 700.5, max_y: 1), nil, 0, &count), RBUSH_OK)
        XCTAssertEqual(count, 0)
        XCTAssertEqual(rbush_remove_identities(tree, [4, 31, 999], 3, &removed), RBUSH_OK)
        XCTAssertEqual(removed, 2)
        XCTAssertEqual(rbush_all(tree, nil, 0, &count), RBUSH_OK)
        XCTAssertEqual(count, 29)
    }
    func testPredicateReallyStopsAtFirstCandidate() throws {
        var index = RBushSpatialIndex<Int>()
        try index.load((0..<1000).map { ($0, box(0)) })
        var calls = 0
        XCTAssertTrue(try index.containsCandidate(in: box(0)) { _ in calls += 1; return true })
        XCTAssertEqual(calls, 1)
        calls = 0
        XCTAssertFalse(try index.containsCandidate(in: box(50)) { _ in calls += 1; return true })
        XCTAssertEqual(calls, 0)
        XCTAssertTrue(try index.containsCandidate(in: box(0)) { $0 == 999 })
    }
    func testBudgetedRegionsCoverAllWithSmallBudgetsAndDoNotMutate() throws {
        var index = RBushSpatialIndex<Int>(maxEntries: 4)
        try index.load((0..<256).map { ($0, box(Double($0 % 16) * 10, Double($0 / 16) * 10)) })
        let root = try XCTUnwrap(index.bounds)
        let before = try index.candidates(in: root)
        XCTAssertEqual(try index.regions(maxRegions: 8, maxNodeVisits: 1, minSpanRatio: 0), [root])
        XCTAssertTrue(try index.regions(maxRegions: 0, maxNodeVisits: 20, minSpanRatio: 0).isEmpty)
        let regions = try index.regions(maxRegions: 16, maxNodeVisits: 32, minSpanRatio: 0.1)
        XCTAssertFalse(regions.isEmpty)
        XCTAssertLessThanOrEqual(regions.count, 16)
        for i in 0..<256 {
            let point = box(Double(i % 16) * 10, Double(i / 16) * 10)
            XCTAssertTrue(regions.contains { $0.minX <= point.minX && $0.minY <= point.minY && $0.maxX >= point.maxX && $0.maxY >= point.maxY })
        }
        XCTAssertEqual(try index.candidates(in: root), before)
    }
    func testConcurrentReadAndFirstWriteUseIndependentNativeGeometry() async throws {
        var built = RBushSpatialIndex<Int>()
        try built.load((0..<256).map { ($0, box(Double($0))) })
        let snapshot = built
        let writable = built
        let reader = Task.detached { () throws -> Bool in
            for _ in 0..<128 {
                guard try snapshot.candidates(in: .init(minX: 99, minY: 0, maxX: 100, maxY: 1)) == [98,99,100] else { return false }
            }
            return snapshot.bounds?.maxX == 256
        }
        let writer = Task.detached { () throws -> RBushSpatialIndex<Int> in
            var value = writable
            for i in 0..<128 { try value.insert(i, bounds: .init(minX: Double(i + 1000), minY: 0, maxX: Double(i + 1001), maxY: 1)) }
            return value
        }
        let readSucceeded = try await reader.value
        let changed = try await writer.value
        XCTAssertTrue(readSucceeded)
        XCTAssertEqual(snapshot.bounds?.maxX, 256)
        XCTAssertEqual(changed.bounds?.maxX, 1128)
        XCTAssertEqual(try snapshot.candidates(in: box(0, 0, 0)), [0])
        XCTAssertTrue(try changed.candidates(in: box(0, 0, 0)).isEmpty)
    }
}
