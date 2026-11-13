import CRBush

private func spatialCheck(_ status: rbush_status) throws {
    guard status == RBUSH_OK else { throw RBushError(String(cString: rbush_last_error())) }
}
private struct SpatialPredicateContext { let matches: (UInt64) -> Bool }
private func spatialPredicate(_ identity: UInt64, _ context: UnsafeMutableRawPointer?) -> Int32 {
    guard let context else { preconditionFailure("Missing predicate context") }
    return context.assumingMemoryBound(to: SpatialPredicateContext.self).pointee.matches(identity) ? 1 : 0
}

/// Value integration over the faithful native tree. Keys map to dense native
/// identities; geometry lives only in the native tree, so a write costs one key
/// lookup here and the rest happens in a single native pass.
/// Copies share read-only storage, cloning native nodes/items on first mutation.
/// That first write is O(N); subsequent writes reuse uniquely owned storage.
public struct RBushSpatialIndex<Key: Hashable> {
    private final class Storage {
        var handle: OpaquePointer?
        let maxEntries: Int
        var tokens: [Key: UInt64] = [:]
        /// Indexed by identity; slot 0 is never issued. Released slots are reused.
        var keys: [Key?] = [nil]
        var freeTokens: [UInt64] = []
        init(maxEntries: Int) { self.maxEntries = maxEntries }
        deinit { rbush_destroy(handle) }
        func cloned() throws -> Storage {
            let result = Storage(maxEntries: maxEntries)
            if let handle { try spatialCheck(rbush_clone(handle, &result.handle)) }
            result.tokens = tokens; result.keys = keys; result.freeTokens = freeTokens
            return result
        }
        func createIfNeeded() throws {
            guard handle == nil else { return }
            guard let maximum = Int32(exactly: maxEntries) else { throw RBushError("maxEntries exceeds Int32 range") }
            try spatialCheck(rbush_create(maximum, &handle))
        }
        /// One hashed write for a new key; a known key keeps its identity.
        func token(for key: Key) -> UInt64 {
            let candidate = freeTokens.last ?? UInt64(keys.count)
            if let existing = tokens.updateValue(candidate, forKey: key) {
                tokens[key] = existing
                return existing
            }
            if freeTokens.isEmpty { keys.append(key) } else { freeTokens.removeLast(); keys[Int(candidate)] = key }
            return candidate
        }
        func release(_ token: UInt64) { keys[Int(token)] = nil; freeTokens.append(token) }
    }
    private var storage: Storage
    public init(maxEntries: Int = 9) { storage = Storage(maxEntries: maxEntries) }
    public var count: Int { storage.tokens.count }
    /// O(1). Nil means empty; finite points and zero-width/height boxes contribute.
    public var bounds: BoundingBox? {
        guard !storage.tokens.isEmpty else { return nil }
        var root = rbush_box(), empty: Int32 = 0
        let status = rbush_root_bounds(storage.handle, &root, &empty)
        precondition(status == RBUSH_OK && empty == 0, "Invalid spatial index storage")
        return .init(minX: root.min_x, minY: root.min_y, maxX: root.max_x, maxY: root.max_y)
    }
    private static func native(_ bounds: BoundingBox, _ token: UInt64) -> rbush_item {
        .init(min_x: bounds.minX, min_y: bounds.minY, max_x: bounds.maxX, max_y: bounds.maxY, identity: token)
    }
    /// Read-only preview for shared storage: a no-op write must not clone.
    private func wouldChange(_ entries: [(Key, BoundingBox)]) throws -> Bool {
        guard let handle = storage.handle else { return true }
        var known: [rbush_item] = []; known.reserveCapacity(entries.count)
        for (key, bounds) in entries {
            guard let token = storage.tokens[key] else { return true }
            known.append(Self.native(bounds, token))
        }
        var changes = 0
        try known.withUnsafeBufferPointer { try spatialCheck(rbush_count_changes(handle, $0.baseAddress, $0.count, &changes)) }
        return changes > 0
    }
    @discardableResult public mutating func insert(_ key: Key, bounds: BoundingBox) throws -> Bool {
        try load([(key, bounds)])
    }
    /// Uses original native bulk loading. Last geometry wins for repeated keys;
    /// raw RBush's independent duplicate-entry semantics remain unchanged.
    @discardableResult public mutating func load(_ entries: [(Key, BoundingBox)]) throws -> Bool {
        guard !entries.isEmpty else { return false }
        if !isKnownUniquelyReferenced(&storage) {
            guard try wouldChange(entries) else { return false }
            storage = try storage.cloned()
        }
        try storage.createIfNeeded()
        let storage = storage
        var native: [rbush_item] = []; native.reserveCapacity(entries.count)
        for (key, bounds) in entries { native.append(Self.native(bounds, storage.token(for: key))) }
        var changed = 0
        try native.withUnsafeBufferPointer { try spatialCheck(rbush_upsert_many(storage.handle, $0.baseAddress, $0.count, &changed)) }
        return changed > 0
    }
    @discardableResult public mutating func remove(_ key: Key) throws -> Bool {
        try removeAll([key])
    }
    @discardableResult public mutating func removeAll(_ keys: [Key]) throws -> Bool {
        guard keys.contains(where: { storage.tokens[$0] != nil }) else { return false }
        if !isKnownUniquelyReferenced(&storage) { storage = try storage.cloned() }
        var released: [UInt64] = []; released.reserveCapacity(keys.count)
        for key in keys { if let token = storage.tokens.removeValue(forKey: key) { released.append(token) } }
        try released.withUnsafeBufferPointer { try spatialCheck(rbush_remove_identities(storage.handle, $0.baseAddress, $0.count, nil)) }
        for token in released { storage.release(token) }
        return true
    }
    public mutating func reset() { storage = Storage(maxEntries: storage.maxEntries) }
    public func candidates(in bounds: BoundingBox) throws -> Set<Key> {
        guard let handle = storage.handle, !storage.tokens.isEmpty else { return [] }
        var result: OpaquePointer?
        try spatialCheck(rbush_search_result(handle, bounds.nativeBox, &result))
        guard let result else { throw RBushError("Missing spatial query result") }
        defer { rbush_result_destroy(result) }
        let count = rbush_result_count(result)
        guard count > 0 else { return [] }
        guard let data = rbush_result_data(result) else { throw RBushError("Missing spatial query buffer") }
        let keys = storage.keys
        return try Set(UnsafeBufferPointer(start: data, count: count).map {
            guard let key = keys[Int($0.identity)] else { throw RBushError("Unknown spatial query token") }
            return key
        })
    }
    public func containsCandidate(in bounds: BoundingBox, where predicate: (Key) -> Bool) throws -> Bool {
        guard let handle = storage.handle, !storage.tokens.isEmpty else { return false }
        return try withoutActuallyEscaping(predicate) { predicate in
            let keys = storage.keys
            var context = SpatialPredicateContext { token in
                guard let key = keys[Int(token)] else { preconditionFailure("Unknown spatial predicate token") }
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
        guard maxRegions > 0, maxNodeVisits > 0, let handle = storage.handle, !storage.tokens.isEmpty else { return [] }
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
