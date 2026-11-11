import XCTest
import CRBush
@testable import RBush

final class RBushTests: XCTestCase {
    func box(_ x: Double, _ y: Double = 0, size: Double = 1) -> BoundingBox {
        BoundingBox(minX: x, minY: y, maxX: x + size, maxY: y + size)
    }
    private func shape(_ node: RBush<Int>.Node) -> String {
        "[" + node.children.map {
            switch $0 { case .item(let item): return String(item.payload); case .node(let node): return shape(node) }
        }.joined(separator: ",") + "]"
    }
    func testCustomizationComparatorsMatchPinnedOracleAndPreserveAcrossClearImport() throws {
        // Pinned RBush oracle: 24 grid items, capacity 4, reverse rank X / rank Y.
        var calls: [String] = []
        let tree = try RBush<Int>(maxEntries: 4,
            compareMinX: { lhs, rhs in calls.append("x:\(lhs.payload):\(rhs.payload)"); return Double(rhs.payload - lhs.payload) },
            compareMinY: { lhs, rhs in calls.append("y:\(lhs.payload):\(rhs.payload)"); return Double(lhs.payload - rhs.payload) })
        let items = (0..<24).map { RBushItem(box(Double($0 % 8), Double($0 / 8)), payload: $0) }
        try tree.load(items)
        XCTAssertEqual(calls.count, 152)
        XCTAssertEqual(Array(calls.prefix(12)), ["y:23:12", "y:1:12", "y:2:12", "y:3:12", "y:4:12", "y:5:12", "y:6:12", "y:7:12", "y:8:12", "y:9:12", "y:10:12", "y:11:12"])
        XCTAssertEqual(shape(try tree.toJSON()), "[[[6,7,8],[9,10,11],[0,1,2],[3,4,5]],[[18,19,20],[21,22,23],[12,13,14],[15,16,17]]]")
        XCTAssertEqual(try tree.search(box(2, 1)).map(\.payload), [17,18,19,3,1,2,9,10,11])
        calls.removeAll()
        try tree.clear().load(items)
        XCTAssertEqual(calls.count, 152)
        try tree.clear()
        let empty = try tree.toJSON()
        try tree.fromJSON(empty)
        calls.removeAll()
        try tree.load(items)
        XCTAssertEqual(calls.count, 152)
        tree.setComparators()
        calls.removeAll()
        try tree.clear().load(items)
        XCTAssertTrue(calls.isEmpty)
        let originalDefaults = try RBush<Int>(maxEntries: 4).load(items)
        XCTAssertEqual(shape(try tree.toJSON()), shape(try originalDefaults.toJSON()))
        XCTAssertEqual(tree.compareMinX(items[0], items[1]), -1)
    }
    func testCustomizationSetterAndSingleAxisInsertHookReleaseContext() throws {
        final class Token {}
        weak var weakTree: RBush<Int>?
        weak var weakToken: Token?
        var calls = 0
        do {
            let tree = try RBush<Int>(maxEntries: 4)
            weakTree = tree
            do {
                let token = Token(); weakToken = token
                tree.setComparators(compareMinX: { [token] lhs, rhs in
                    _ = token; calls += 1; return Double(lhs.payload - rhs.payload)
                })
            }
            XCTAssertNotNil(weakToken)
            for i in 0..<24 { try tree.insert(RBushItem(box(Double(i)), payload: i)) }
            XCTAssertGreaterThan(calls, 0)
            let before = calls
            XCTAssertEqual(tree.compareMinX(RBushItem(box(9), payload: 4), RBushItem(box(0), payload: 1)), 3)
            XCTAssertEqual(calls, before + 1)
            XCTAssertEqual(tree.compareMinY(RBushItem(box(0, 9), payload: 4), RBushItem(box(0, 1), payload: 1)), 8)
            tree.setComparators()
            XCTAssertNil(weakToken)
        }
        XCTAssertNil(weakTree)
    }
    func testCustomizationBBoxProjectionInputRemovalImportAndReset() throws {
        let projected = box(4)
        let item = RBushItem(box(99), payload: projected)
        let tree = try RBush<BoundingBox>(maxEntries: 4, toBBox: { $0.payload })
        XCTAssertEqual(tree.toBBox(item), projected)
        try tree.insert(item)
        XCTAssertTrue(try tree.search(projected).first === item)
        XCTAssertFalse(try tree.collides(box(99)))
        let snapshot = try tree.toJSON()
        try tree.clear().fromJSON(snapshot)
        XCTAssertTrue(try tree.search(projected).first === item)
        try tree.remove(item)
        item.payload = box(20)
        try tree.insert(item)
        XCTAssertTrue(try tree.search(projected).isEmpty)
        XCTAssertTrue(try tree.search(box(20)).first === item)
        try tree.clear()
        tree.setToBBox(nil)
        XCTAssertEqual(tree.toBBox(item), box(99))
        try tree.insert(item)
        XCTAssertTrue(try tree.search(box(99)).first === item)
        XCTAssertFalse(try tree.collides(box(20)))
    }
    func testCBufferFailureReportsDiagnosticAndLeavesBufferUntouched() throws {
        var tree: OpaquePointer?
        XCTAssertEqual(rbush_create(9, &tree), RBUSH_OK)
        defer { rbush_destroy(tree) }
        let item = rbush_item(min_x: 0, min_y: 0, max_x: 1, max_y: 1, identity: 8)
        XCTAssertEqual(rbush_insert(tree, item), RBUSH_OK)
        var sentinel = rbush_item(min_x: 42, min_y: 42, max_x: 42, max_y: 42, identity: 42)
        var count = 0
        XCTAssertEqual(rbush_all(tree, &sentinel, 0, &count), RBUSH_BUFFER_TOO_SMALL)
        XCTAssertEqual(count, 1)
        XCTAssertEqual(sentinel.identity, 42)
        XCTAssertFalse(String(cString: rbush_last_error()).isEmpty)
    }
    func testCOwnedQuerySurvivesTreeDestruction() throws {
        var tree: OpaquePointer?, result: OpaquePointer?
        XCTAssertEqual(rbush_create(9, &tree), RBUSH_OK)
        XCTAssertEqual(rbush_insert(tree, .init(min_x: 0, min_y: 0, max_x: 1, max_y: 1, identity: 99)), RBUSH_OK)
        XCTAssertEqual(rbush_all_result(tree, &result), RBUSH_OK)
        rbush_destroy(tree)
        defer { rbush_result_destroy(result) }
        XCTAssertEqual(rbush_result_count(result), 1)
        XCTAssertEqual(rbush_result_data(result)?.pointee.identity, 99)
    }
    func testRemoveBeforeMutationPreservesReferenceOnReinsert() throws {
        let tree = try RBush<Int>()
        let item = RBushItem(box(0), payload: 0)
        try tree.insert(item).remove(item)
        item.boundingBox = box(10)
        item.payload = 1
        try tree.insert(item).remove(nil)
        XCTAssertTrue(try tree.search(box(0)).isEmpty)
        XCTAssertTrue(try tree.search(box(10)).first === item)
        XCTAssertEqual(try tree.all().first?.payload, 1)
    }
    func testLastRemovalAndClearReleasePayloads() throws {
        let tree = try RBush<Int>()
        weak var removed: RBushItem<Int>?
        do {
            let item = RBushItem(box(0), payload: 0)
            removed = item
            try tree.insert(item).insert(item).remove(item)
            XCTAssertNotNil(removed)
            try tree.remove(item)
        }
        XCTAssertNil(removed)
        weak var cleared: RBushItem<Int>?
        do {
            let item = RBushItem(box(0), payload: 0)
            cleared = item
            try tree.insert(item)
        }
        XCTAssertNotNil(cleared)
        try tree.clear()
        XCTAssertNil(cleared)
    }
    func testDefaultCapacityAndConstructorClamp() throws {
        let tree = try RBush<Int>()
        XCTAssertEqual(tree.maxEntries, 9)
        XCTAssertEqual(tree.minEntries, 4)
        let small = try RBush<Int>(maxEntries: 1)
        XCTAssertEqual(small.maxEntries, 4)
        XCTAssertEqual(small.minEntries, 2)
    }
    func testInsertSearchAndInclusiveDegenerateBoxes() throws {
        let tree = try RBush<String>()
        let point = RBushItem(box(1, 1, size: 0), payload: "point")
        let regular = RBushItem(box(0), payload: "regular")
        try tree.insert(point).insert(regular)
        XCTAssertEqual(try tree.all().count, 2)
        XCTAssertEqual(Set(try tree.search(box(1, 1, size: 0)).map(\.payload)), ["point", "regular"])
        XCTAssertTrue(try tree.collides(box(1, 1, size: 0)))
        XCTAssertFalse(try tree.collides(box(9)))
    }
    func testDefaultRemovalUsesReferenceIdentityAndKeepsDuplicates() throws {
        let tree = try RBush<Int>()
        let original = RBushItem(box(0), payload: 1)
        let equalValue = RBushItem(box(0), payload: 1)
        try tree.load([original, original, equalValue])
        try tree.remove(RBushItem(box(0), payload: 1))
        XCTAssertEqual(try tree.all().count, 3)
        try tree.remove(original)
        let remaining = try tree.all()
        XCTAssertEqual(remaining.count, 2)
        XCTAssertEqual(remaining.filter { $0 === original }.count, 1)
        XCTAssertEqual(remaining.filter { $0 === equalValue }.count, 1)
    }
    func testCustomEqualityReceivesRequestedThenCandidate() throws {
        let tree = try RBush<Int>()
        let stored = RBushItem(box(0), payload: 1)
        let requested = RBushItem(box(0), payload: 9)
        try tree.insert(stored)
        var pairs: [(Int, Int)] = []
        try tree.remove(requested) { lhs, rhs in
            pairs.append((lhs.payload, rhs.payload)); return lhs.payload == 9 && rhs.payload == 1
        }
        XCTAssertEqual(pairs.map { $0.0 }, [9])
        XCTAssertEqual(pairs.map { $0.1 }, [1])
        XCTAssertTrue(try tree.all().isEmpty)
    }
    func testLoadAndClearResetEmptyInfinityBounds() throws {
        let tree = try RBush<Int>()
        let items = (0..<100).map { RBushItem(box(Double($0)), payload: $0) }
        try tree.load(items)
        XCTAssertEqual(try tree.search(box(15, size: 5)).count, 7)
        try tree.clear()
        XCTAssertTrue(try tree.all().isEmpty)
        let empty = try tree.toJSON()
        XCTAssertEqual(empty.height, 1)
        XCTAssertTrue(empty.leaf)
        XCTAssertEqual(empty.boundingBox.minX, .infinity)
        XCTAssertEqual(empty.boundingBox.maxX, -.infinity)
    }
    func testJSONRoundTripPreservesExactShapeOrderAndItemIdentity() throws {
        let tree = try RBush<Int>(maxEntries: 4)
        let items = (0..<100).map { RBushItem(box(Double($0), Double($0 % 7)), payload: $0) }
        try tree.load(items)
        let json = try tree.toJSON()
        XCTAssertGreaterThan(json.height, 1)
        let restored = try RBush<Int>(maxEntries: 4).fromJSON(json)
        func signature(_ node: RBush<Int>.Node) -> String {
            "\(node.boundingBox)|\(node.height)|\(node.leaf)|" + node.children.map {
                switch $0 { case .item(let item): return "i\(item.payload)"; case .node(let child): return signature(child) }
            }.joined(separator: ",")
        }
        XCTAssertEqual(signature(json), signature(try restored.toJSON()))
        let found = try restored.search(box(21, 0, size: 0))
        XCTAssertEqual(found.count, 1)
        XCTAssertTrue(found.first === items[21])
        try restored.remove(items[21])
        XCTAssertEqual(try restored.all().count, 99)
    }
    func testFromJSONInvalidHeightFailsWithoutChangingTree() throws {
        let tree = try RBush<Int>()
        let original = RBushItem(box(0), payload: 0)
        try tree.insert(original)
        let impossible = RBush<Int>.Node(boundingBox: box(0), height: 2, leaf: true, children: [])
        XCTAssertThrowsError(try tree.fromJSON(impossible))
        XCTAssertTrue(try tree.all().first === original)
        try tree.insert(RBushItem(box(5), payload: 5))
        XCTAssertEqual(try tree.all().count, 2)
    }
    func testFromJSONInvalidChildTypeFailsWithoutChangingTree() throws {
        let tree = try RBush<Int>()
        let item = RBushItem(box(0), payload: 0)
        try tree.insert(item)
        let invalid = RBush<Int>.Node(boundingBox: box(0), height: 1, leaf: true,
            children: [.node(.init(boundingBox: box(0), height: 1, leaf: true, children: []))])
        XCTAssertThrowsError(try tree.fromJSON(invalid))
        XCTAssertTrue(try tree.all().first === item)
    }
}
