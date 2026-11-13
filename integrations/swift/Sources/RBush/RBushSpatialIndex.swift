/// Value integration for arbitrary hashable keys, layered on `RBushIdentityIndex`.
/// Keys map to dense native identities; geometry lives only in the native tree,
/// so a write costs one hashed key operation here on top of the native pass.
/// Copies share read-only storage, cloning native nodes/items on first mutation.
/// That first write is O(N); subsequent writes reuse uniquely owned storage.
public struct RBushSpatialIndex<Key: Hashable> {
    private final class Storage {
        var core: RBushIdentityIndex
        var tokens: [Key: UInt64] = [:]
        /// Indexed by identity; slot 0 is never issued. Released slots are reused.
        var keys: [Key?] = [nil]
        var freeTokens: [UInt64] = []
        init(core: RBushIdentityIndex) { self.core = core }
        /// The native tree stays shared until the copy's first real write.
        func cloned() -> Storage {
            let result = Storage(core: core)
            result.tokens = tokens; result.keys = keys; result.freeTokens = freeTokens
            return result
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
    private let maxEntries: Int
    public init(maxEntries: Int = 9) {
        self.maxEntries = maxEntries
        storage = Storage(core: RBushIdentityIndex(maxEntries: maxEntries))
    }
    public var count: Int { storage.tokens.count }
    /// O(1). Nil means empty; finite points and zero-width/height boxes contribute.
    public var bounds: BoundingBox? { storage.core.bounds }
    /// Read-only preview for shared storage: a no-op write must not clone.
    private func wouldChange(_ entries: [(Key, BoundingBox)]) throws -> Bool {
        var known: [RBushIdentityEntry] = []; known.reserveCapacity(entries.count)
        for (key, bounds) in entries {
            guard let token = storage.tokens[key] else { return true }
            known.append(.init(identity: token, bounds: bounds))
        }
        return try storage.core.wouldChange(known)
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
            storage = storage.cloned()
        }
        let storage = storage
        var native: [RBushIdentityEntry] = []; native.reserveCapacity(entries.count)
        for (key, bounds) in entries { native.append(.init(identity: storage.token(for: key), bounds: bounds)) }
        return try storage.core.upsert(native)
    }
    @discardableResult public mutating func remove(_ key: Key) throws -> Bool {
        try removeAll([key])
    }
    @discardableResult public mutating func removeAll(_ keys: [Key]) throws -> Bool {
        guard keys.contains(where: { storage.tokens[$0] != nil }) else { return false }
        if !isKnownUniquelyReferenced(&storage) { storage = storage.cloned() }
        var released: [UInt64] = []; released.reserveCapacity(keys.count)
        for key in keys { if let token = storage.tokens.removeValue(forKey: key) { released.append(token) } }
        try storage.core.remove(released)
        for token in released { storage.release(token) }
        return true
    }
    public mutating func reset() { storage = Storage(core: RBushIdentityIndex(maxEntries: maxEntries)) }
    public func candidates(in bounds: BoundingBox) throws -> Set<Key> {
        guard !storage.tokens.isEmpty else { return [] }
        let keys = storage.keys
        return try Set(storage.core.candidates(in: bounds).map {
            guard let key = keys[Int($0)] else { throw RBushError("Unknown spatial query token") }
            return key
        })
    }
    public func containsCandidate(in bounds: BoundingBox, where predicate: (Key) -> Bool) throws -> Bool {
        guard !storage.tokens.isEmpty else { return false }
        let keys = storage.keys
        return try storage.core.containsCandidate(in: bounds) { token in
            guard let key = keys[Int(token)] else { preconditionFailure("Unknown spatial predicate token") }
            return predicate(key)
        }
    }
    public func regions(maxRegions: Int, maxNodeVisits: Int, minSpanRatio: Double) throws -> [BoundingBox] {
        try storage.core.regions(maxRegions: maxRegions, maxNodeVisits: maxNodeVisits, minSpanRatio: minSpanRatio)
    }
}
// Only the value facade crosses concurrency boundaries. Shared Storage is never
// mutated: every write checks uniqueness and deep-clones native geometry first.
// Separate copies can therefore read/write concurrently when their keys are safe.
extension RBushSpatialIndex: @unchecked Sendable where Key: Sendable {}
