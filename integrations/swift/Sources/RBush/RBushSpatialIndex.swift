import CRBush

private func spatialCheck(_ status: rbush_status) throws {
    guard status == RBUSH_OK else { throw RBushError(String(cString: rbush_last_error())) }
}
private struct SpatialPredicateContext { let matches: (UInt64) -> Bool }
private func spatialPredicate(_ identity: UInt64, _ context: UnsafeMutableRawPointer?) -> Int32 {
    guard let context else { preconditionFailure("Missing predicate context") }
    return context.assumingMemoryBound(to: SpatialPredicateContext.self).pointee.matches(identity) ? 1 : 0
}

/// Value integration over the faithful native tree. Keys and immutable geometry
/// records are owned here; no mutable RBushItem or native handle can escape.
/// Copies share read-only storage, cloning native nodes/items on first mutation.
/// That first write is O(N); subsequent writes reuse uniquely owned storage.
public struct RBushSpatialIndex<Key: Hashable> {
    private struct Record {
        let token: UInt64
        let bounds: BoundingBox
        var native: rbush_item {
            .init(min_x: bounds.minX, min_y: bounds.minY, max_x: bounds.maxX, max_y: bounds.maxY, identity: token)
        }
    }
    private final class Storage {
        var handle: OpaquePointer?
        let maxEntries: Int
        var records: [Key: Record] = [:]
        var keys: [UInt64: Key] = [:]
        var nextToken: UInt64 = 1
        init(maxEntries: Int) { self.maxEntries = maxEntries }
        deinit { rbush_destroy(handle) }
        func cloned() throws -> Storage {
            let result = Storage(maxEntries: maxEntries)
            if let handle { try spatialCheck(rbush_clone(handle, &result.handle)) }
            result.records = records; result.keys = keys; result.nextToken = nextToken
            return result
        }
        func createIfNeeded() throws {
            guard handle == nil else { return }
            guard let maximum = Int32(exactly: maxEntries) else { throw RBushError("maxEntries exceeds Int32 range") }
            try spatialCheck(rbush_create(maximum, &handle))
        }
    }
    private var storage: Storage
    public init(maxEntries: Int = 9) { storage = Storage(maxEntries: maxEntries) }
    public var count: Int { storage.records.count }
    /// O(1). Nil means empty; finite points and zero-width/height boxes contribute.
    public var bounds: BoundingBox? {
        guard !storage.records.isEmpty else { return nil }
        var root = rbush_box(), empty: Int32 = 0
        let status = rbush_root_bounds(storage.handle, &root, &empty)
        precondition(status == RBUSH_OK && empty == 0, "Invalid spatial index storage")
        return .init(minX: root.min_x, minY: root.min_y, maxX: root.max_x, maxY: root.max_y)
    }
    private mutating func ensureWritable() throws {
        if !isKnownUniquelyReferenced(&storage) { storage = try storage.cloned() }
        try storage.createIfNeeded()
    }
    private func newRecord(bounds: BoundingBox, existing: Record?) throws -> Record {
        if let existing { return Record(token: existing.token, bounds: bounds) }
        guard storage.nextToken < UInt64.max else { throw RBushError("Spatial identity space exhausted") }
        let token = storage.nextToken; storage.nextToken += 1
        return Record(token: token, bounds: bounds)
    }
    @discardableResult public mutating func insert(_ key: Key, bounds: BoundingBox) throws -> Bool {
        guard storage.records[key]?.bounds != bounds else { return false }
        try ensureWritable()
        let old = storage.records[key]
        let replacement = try newRecord(bounds: bounds, existing: old)
        if let old { try spatialCheck(rbush_remove(storage.handle, old.native, nil, nil)) }
        try spatialCheck(rbush_insert(storage.handle, replacement.native))
        storage.records[key] = replacement; storage.keys[replacement.token] = key
        return true
    }
    /// Uses original native bulk loading. Last geometry wins for repeated keys;
    /// raw RBush's independent duplicate-entry semantics remain unchanged.
    @discardableResult public mutating func load(_ entries: [(Key, BoundingBox)]) throws -> Bool {
        var final: [Key: BoundingBox] = [:], order: [Key] = []
        for (key, bounds) in entries {
            if final[key] == nil { order.append(key) }
            final[key] = bounds
        }
        let changed = order.filter { storage.records[$0]?.bounds != final[$0] }
        guard !changed.isEmpty else { return false }
        try ensureWritable()
        var records: [(Key, Record)] = []; records.reserveCapacity(changed.count)
        for key in changed {
            guard let bounds = final[key] else { preconditionFailure("Missing batch bounds") }
            let old = storage.records[key]
            let replacement = try newRecord(bounds: bounds, existing: old)
            if let old { try spatialCheck(rbush_remove(storage.handle, old.native, nil, nil)) }
            records.append((key, replacement))
        }
        let native = records.map { $0.1.native }
        try native.withUnsafeBufferPointer { try spatialCheck(rbush_load(storage.handle, $0.baseAddress, $0.count)) }
        for (key, record) in records { storage.records[key] = record; storage.keys[record.token] = key }
        return true
    }
    @discardableResult public mutating func remove(_ key: Key) throws -> Bool {
        guard storage.records[key] != nil else { return false }
        try ensureWritable()
        guard let record = storage.records[key] else { preconditionFailure("Missing removal record") }
        try spatialCheck(rbush_remove(storage.handle, record.native, nil, nil))
        storage.records.removeValue(forKey: key); storage.keys.removeValue(forKey: record.token)
        return true
    }
    @discardableResult public mutating func removeAll(_ keys: [Key]) throws -> Bool {
        let present = Set(keys).filter { storage.records[$0] != nil }
        guard !present.isEmpty else { return false }
        try ensureWritable()
        for key in present { try remove(key) }
        return true
    }
    public mutating func reset() { storage = Storage(maxEntries: storage.maxEntries) }
    public func candidates(in bounds: BoundingBox) throws -> Set<Key> {
        guard let handle = storage.handle, !storage.records.isEmpty else { return [] }
        var result: OpaquePointer?
        try spatialCheck(rbush_search_result(handle, bounds.nativeBox, &result))
        guard let result else { throw RBushError("Missing spatial query result") }
        defer { rbush_result_destroy(result) }
        let count = rbush_result_count(result)
        guard count > 0 else { return [] }
        guard let data = rbush_result_data(result) else { throw RBushError("Missing spatial query buffer") }
        return try Set(UnsafeBufferPointer(start: data, count: count).map {
            guard let key = storage.keys[$0.identity] else { throw RBushError("Unknown spatial query token") }
            return key
        })
    }
    public func containsCandidate(in bounds: BoundingBox, where predicate: (Key) -> Bool) throws -> Bool {
        guard let handle = storage.handle, !storage.records.isEmpty else { return false }
        return try withoutActuallyEscaping(predicate) { predicate in
            let keys = storage.keys
            var context = SpatialPredicateContext { token in
                guard let key = keys[token] else { preconditionFailure("Unknown spatial predicate token") }
                return predicate(key)
            }
            var found: Int32 = 0
            try withUnsafeMutablePointer(to: &context) { pointer in
                try spatialCheck(rbush_any(handle, bounds.nativeBox, spatialPredicate, pointer, &found, nil, nil))
            }
            return found != 0
        }
    }
    public func regions(maxRegions: Int, maxNodeVisits: Int, minSpanRatio: Double) throws -> [BoundingBox] {
        guard minSpanRatio.isFinite && minSpanRatio >= 0 else { throw RBushError("minSpanRatio must be finite and nonnegative") }
        guard maxRegions > 0, maxNodeVisits > 0, let handle = storage.handle, !storage.records.isEmpty else { return [] }
        var regions = [rbush_box](repeating: rbush_box(), count: maxRegions), count = 0
        try regions.withUnsafeMutableBufferPointer {
            try spatialCheck(rbush_regions(handle, maxRegions, maxNodeVisits, minSpanRatio, $0.baseAddress, $0.count, &count, nil))
        }
        return regions.prefix(count).map { .init(minX: $0.min_x, minY: $0.min_y, maxX: $0.max_x, maxY: $0.max_y) }
    }
}
private extension BoundingBox {
    var nativeBox: rbush_box { .init(min_x: minX, min_y: minY, max_x: maxX, max_y: maxY) }
}
// Only the value facade crosses concurrency boundaries. Shared Storage is never
// mutated: every write checks uniqueness and deep-clones native geometry first.
// Separate copies can therefore read/write concurrently when their keys are safe.
extension RBushSpatialIndex: @unchecked Sendable where Key: Sendable {}
