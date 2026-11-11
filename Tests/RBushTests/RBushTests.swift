import XCTest
import CRBush
@testable import RBush

final class RBushTests: XCTestCase {
    func box(_ x: Double, _ y: Double = 0, size: Double = 1) -> BoundingBox {
        BoundingBox(minX: x, minY: y, maxX: x + size, maxY: y + size)
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
