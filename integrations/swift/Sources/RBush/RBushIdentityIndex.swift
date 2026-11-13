import CRBush

func spatialCheck(_ status: rbush_status) throws {
    guard status == RBUSH_OK else { throw RBushError(String(cString: rbush_last_error())) }
}
private struct IdentityPredicateContext { let matches: (UInt64) -> Bool }
private func identityPredicate(_ identity: UInt64, _ context: UnsafeMutableRawPointer?) -> Int32 {
    guard let context else { preconditionFailure("Missing predicate context") }
    return context.assumingMemoryBound(to: IdentityPredicateContext.self).pointee.matches(identity) ? 1 : 0
}
extension BoundingBox {
    var nativeBox: rbush_box { .init(min_x: minX, min_y: minY, max_x: maxX, max_y: maxY) }
}

/// One box addressed by a caller-owned integer identity. Field order matches the
/// C ABI item, so a batch crosses the boundary as-is, without per-entry conversion.
public struct RBushIdentityEntry: Equatable, Sendable {
    public var bounds: BoundingBox
    public var identity: UInt64
    public init(identity: UInt64, bounds: BoundingBox) { self.bounds = bounds; self.identity = identity }
}

/// Value index for callers that already own stable integer identities. Nothing is
/// mirrored in Swift: unchanged-box skipping, moves, last-wins for a repeated
/// identity and bulk loading all happen in one native pass, so a write costs what
/// the native tree costs. Copies share read-only storage and clone on first write.
public struct RBushIdentityIndex: @unchecked Sendable {
    private final class Storage {
        var handle: OpaquePointer?
        let maxEntries: Int
        init(maxEntries: Int) { self.maxEntries = maxEntries }
        deinit { rbush_destroy(handle) }
        func cloned() throws -> Storage {
            let result = Storage(maxEntries: maxEntries)
            if let handle { try spatialCheck(rbush_clone(handle, &result.handle)) }
            return result
        }
        func createIfNeeded() throws {
            guard handle == nil else { return }
            guard let maximum = Int32(exactly: maxEntries) else { throw RBushError("maxEntries exceeds Int32 range") }
            try spatialCheck(rbush_create(maximum, &handle))
        }
    }
    private var storage: Storage
    public init(maxEntries: Int = 9) {
        precondition(MemoryLayout<RBushIdentityEntry>.stride == MemoryLayout<rbush_item>.stride
            && MemoryLayout<RBushIdentityEntry>.offset(of: \.identity) == MemoryLayout<rbush_item>.offset(of: \.identity),
            "RBushIdentityEntry must match the C ABI item layout")
        storage = Storage(maxEntries: maxEntries)
    }
    public var count: Int {
        guard let handle = storage.handle else { return 0 }
        var count = 0
        precondition(rbush_identity_count(handle, &count) == RBUSH_OK, "Invalid spatial index storage")
        return count
    }
    public var isEmpty: Bool { count == 0 }
    /// O(1). Nil means empty; finite points and zero-width/height boxes contribute.
    public var bounds: BoundingBox? {
        guard !isEmpty else { return nil }
        var root = rbush_box(), empty: Int32 = 0
        let status = rbush_root_bounds(storage.handle, &root, &empty)
        precondition(status == RBUSH_OK && empty == 0, "Invalid spatial index storage")
        return .init(minX: root.min_x, minY: root.min_y, maxX: root.max_x, maxY: root.max_y)
    }
    private static func withNative<Result>(
        _ entries: [RBushIdentityEntry], _ body: (UnsafePointer<rbush_item>?, Int) throws -> Result
    ) rethrows -> Result {
        try entries.withUnsafeBytes { try body($0.bindMemory(to: rbush_item.self).baseAddress, entries.count) }
    }
    /// Read-only preview of `upsert`: true when at least one entry is new or moved.
    public func wouldChange(_ entries: [RBushIdentityEntry]) throws -> Bool {
        guard !entries.isEmpty else { return false }
        guard let handle = storage.handle else { return true }
        var changes = 0
        try Self.withNative(entries) { try spatialCheck(rbush_count_changes(handle, $0, $1, &changes)) }
        return changes > 0
    }
    /// Inserts new identities and moves changed ones. Last box wins for a repeated
    /// identity. A no-op batch never clones shared storage.
    @discardableResult public mutating func upsert(_ entries: [RBushIdentityEntry]) throws -> Bool {
        guard !entries.isEmpty else { return false }
        if !isKnownUniquelyReferenced(&storage) {
            guard try wouldChange(entries) else { return false }
            storage = try storage.cloned()
        }
        try storage.createIfNeeded()
        let handle = storage.handle
        var changed = 0
        try Self.withNative(entries) { try spatialCheck(rbush_upsert_many(handle, $0, $1, &changed)) }
        return changed > 0
    }
    /// Unknown identities are ignored. Returns whether anything was removed.
    @discardableResult public mutating func remove(_ identities: [UInt64]) throws -> Bool {
        guard !identities.isEmpty, storage.handle != nil else { return false }
        if !isKnownUniquelyReferenced(&storage) { storage = try storage.cloned() }
        let handle = storage.handle
        var removed = 0
        try identities.withUnsafeBufferPointer {
            try spatialCheck(rbush_remove_identities(handle, $0.baseAddress, $0.count, &removed))
        }
        return removed > 0
    }
    public mutating func reset() { storage = Storage(maxEntries: storage.maxEntries) }
    /// Each matching identity exactly once, in native traversal order.
    public func candidates(in bounds: BoundingBox) throws -> [UInt64] {
        guard let handle = storage.handle else { return [] }
        var result: OpaquePointer?
        try spatialCheck(rbush_search_result(handle, bounds.nativeBox, &result))
        guard let result else { throw RBushError("Missing spatial query result") }
        defer { rbush_result_destroy(result) }
        let count = rbush_result_count(result)
        guard count > 0 else { return [] }
        guard let data = rbush_result_data(result) else { throw RBushError("Missing spatial query buffer") }
        return [UInt64](unsafeUninitializedCapacity: count) { buffer, initialized in
            for index in 0..<count { buffer[index] = data[index].identity }
            initialized = count
        }
    }
    /// Synchronous native visit; stops at the first identity the predicate accepts.
    public func containsCandidate(in bounds: BoundingBox, where predicate: (UInt64) -> Bool) throws -> Bool {
        guard let handle = storage.handle else { return false }
        return try withoutActuallyEscaping(predicate) { predicate in
            var context = IdentityPredicateContext(matches: predicate)
            var found: Int32 = 0
            try withUnsafeMutablePointer(to: &context) { pointer in
                try spatialCheck(rbush_any(handle, bounds.nativeBox, identityPredicate, pointer, &found, nil, nil))
            }
            return found != 0
        }
    }
    public func regions(maxRegions: Int, maxNodeVisits: Int, minSpanRatio: Double) throws -> [BoundingBox] {
        guard minSpanRatio.isFinite && minSpanRatio >= 0 else { throw RBushError("minSpanRatio must be finite and nonnegative") }
        guard maxRegions > 0, maxNodeVisits > 0, let handle = storage.handle, !isEmpty else { return [] }
        var regions = [rbush_box](repeating: rbush_box(), count: maxRegions), count = 0
        try regions.withUnsafeMutableBufferPointer {
            try spatialCheck(rbush_regions(handle, maxRegions, maxNodeVisits, minSpanRatio, $0.baseAddress, $0.count, &count, nil))
        }
        return regions.prefix(count).map { .init(minX: $0.min_x, minY: $0.min_y, maxX: $0.max_x, maxY: $0.max_y) }
    }
}
