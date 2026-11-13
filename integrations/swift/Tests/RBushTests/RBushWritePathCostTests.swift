import XCTest
import CRBush
import RBush

/// Layered write-path cost: the same N boxes through the C ABI directly and
/// through the Swift value facade. Opt-in (RBUSH_BENCH=1); prints, never asserts timing.
final class RBushWritePathCostTests: XCTestCase {
    private enum RecordKey: Hashable { case item(UUID), stroke(UUID) }
    private let count = 5_000

    private func box(_ index: Int) -> BoundingBox {
        let x = Double(index % 100) * 7, y = Double(index / 100) * 7
        return .init(minX: x, minY: y, maxX: x + 12, maxY: y + 12)
    }
    private func native(_ index: Int) -> rbush_item {
        let b = box(index)
        return .init(min_x: b.minX, min_y: b.minY, max_x: b.maxX, max_y: b.maxY, identity: UInt64(index + 1))
    }
    private func time(_ label: String, _ body: () throws -> Void) rethrows {
        var best = Double.infinity
        for _ in 0..<40 {
            let started = DispatchTime.now().uptimeNanoseconds
            try body()
            best = min(best, Double(DispatchTime.now().uptimeNanoseconds - started) / 1_000)
        }
        print(String(format: "RBUSH-WritePath %@ bestTotalMs=%.3f perEntryUs=%.3f",
            label, best / 1_000, best / Double(count)))
    }

    func testLayeredWritePathCost() throws {
        guard ProcessInfo.processInfo.environment["RBUSH_BENCH"] == "1" else { throw XCTSkip("opt-in benchmark") }
        let items = (0..<count).map(native)
        let boxes = (0..<count).map(box)
        let keys = (0..<count).map { _ in RecordKey.stroke(UUID()) }

        let intEntries = (0..<count).map { (UInt64($0), boxes[$0]) }
        let enumEntries = (0..<count).map { (keys[$0], boxes[$0]) }
        let enumBatches = (0..<10).map { Array(enumEntries[($0 * count / 10)..<(($0 + 1) * count / 10)]) }
        let itemBatches = (0..<10).map { Array(items[($0 * count / 10)..<(($0 + 1) * count / 10)]) }
        time("c-abi-upsert-many") {
            var tree: OpaquePointer?
            rbush_create(9, &tree)
            items.withUnsafeBufferPointer { _ = rbush_upsert_many(tree, $0.baseAddress, $0.count, nil) }
            rbush_destroy(tree)
        }
        // Floor for a caller that owns integer identities: only box -> native item conversion in Swift.
        time("swift-identity-floor") {
            var tree: OpaquePointer?
            rbush_create(9, &tree)
            var native: [rbush_item] = []; native.reserveCapacity(count)
            for (identity, b) in intEntries {
                native.append(.init(min_x: b.minX, min_y: b.minY, max_x: b.maxX, max_y: b.maxY, identity: identity + 1))
            }
            native.withUnsafeBufferPointer { _ = rbush_upsert_many(tree, $0.baseAddress, $0.count, nil) }
            rbush_destroy(tree)
        }
        let identityEntries = (0..<count).map { RBushIdentityEntry(identity: UInt64($0 + 1), bounds: boxes[$0]) }
        let identityBatches = (0..<10).map { Array(identityEntries[($0 * count / 10)..<(($0 + 1) * count / 10)]) }
        try time("swift-identity-upsert") {
            var index = RBushIdentityIndex()
            try index.upsert(identityEntries)
        }
        try time("swift-identity-upsert-10-batches") {
            var index = RBushIdentityIndex()
            for batch in identityBatches { try index.upsert(batch) }
        }
        time("c-abi-insert") {
            var tree: OpaquePointer?
            rbush_create(9, &tree)
            for item in items { rbush_insert(tree, item) }
            rbush_destroy(tree)
        }
        time("c-abi-load") {
            var tree: OpaquePointer?
            rbush_create(9, &tree)
            items.withUnsafeBufferPointer { _ = rbush_load(tree, $0.baseAddress, $0.count) }
            rbush_destroy(tree)
        }
        try time("swift-int-insert") {
            var index = RBushSpatialIndex<UInt64>()
            for i in 0..<count { try index.insert(UInt64(i), bounds: boxes[i]) }
        }
        try time("swift-int-load") {
            var index = RBushSpatialIndex<UInt64>()
            try index.load(intEntries)
        }
        try time("swift-enum-insert") {
            var index = RBushSpatialIndex<RecordKey>()
            for i in 0..<count { try index.insert(keys[i], bounds: boxes[i]) }
        }
        try time("swift-enum-load") {
            var index = RBushSpatialIndex<RecordKey>()
            try index.load(enumEntries)
        }
        // Incremental shape used by the canvas: 10 batches of N/10 into a growing tree.
        try time("swift-enum-load-10-batches") {
            var index = RBushSpatialIndex<RecordKey>()
            for batch in enumBatches { try index.load(batch) }
        }
        time("c-abi-load-10-batches") {
            var tree: OpaquePointer?
            rbush_create(9, &tree)
            for batch in itemBatches { batch.withUnsafeBufferPointer { _ = rbush_load(tree, $0.baseAddress, $0.count) } }
            rbush_destroy(tree)
        }
    }
}
