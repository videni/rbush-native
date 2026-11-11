import CRBush

public struct BoundingBox: Equatable, Codable, Sendable {
    public var minX, minY, maxX, maxY: Double
    public init(minX: Double, minY: Double, maxX: Double, maxY: Double) {
        self.minX = minX; self.minY = minY; self.maxX = maxX; self.maxY = maxY
    }
    fileprivate var native: rbush_box { rbush_box(min_x: minX, min_y: minY, max_x: maxX, max_y: maxY) }
}

/// A reference is an upstream object identity. Equal payloads or boxes do not
/// imply equal identity. Remove before changing the box of an indexed item,
/// then insert the same object again; bounds are projected across the C ABI.
public final class RBushItem<Payload> {
    public var boundingBox: BoundingBox
    public var payload: Payload
    public init(_ boundingBox: BoundingBox, payload: Payload) {
        self.boundingBox = boundingBox; self.payload = payload
    }
}

public struct RBushError: Error, CustomStringConvertible {
    public let description: String
    init(_ description: String) { self.description = description }
}

private func check(_ status: rbush_status) throws {
    guard status == RBUSH_OK else { throw RBushError(String(cString: rbush_last_error())) }
}

private struct EqualityContext {
    let matches: (UInt64) -> Bool
}
private func equalityCallback(_ requested: UInt64, _ candidate: UInt64, _ raw: UnsafeMutableRawPointer?) -> Int32 {
    guard let raw else { return 0 }
    return raw.assumingMemoryBound(to: EqualityContext.self).pointee.matches(candidate) ? 1 : 0
}

// Stack-owned during one synchronous C operation. C stores only its raw
// address for that operation and never retains a Swift closure or object.
private struct ComparisonContext {
    let x: ((UInt64, UInt64) -> Double)?
    let y: ((UInt64, UInt64) -> Double)?
}
private func compareXCallback(_ lhs: UInt64, _ rhs: UInt64, _ raw: UnsafeMutableRawPointer?) -> Double {
    guard let raw else { preconditionFailure("Missing comparison context") }
    return raw.assumingMemoryBound(to: ComparisonContext.self).pointee.x?(lhs, rhs) ?? 0
}
private func compareYCallback(_ lhs: UInt64, _ rhs: UInt64, _ raw: UnsafeMutableRawPointer?) -> Double {
    guard let raw else { preconditionFailure("Missing comparison context") }
    return raw.assumingMemoryBound(to: ComparisonContext.self).pointee.y?(lhs, rhs) ?? 0
}

/// Thin reference-type owner of the C++ RBush. This type is not synchronized;
/// serialize access just as for the upstream mutable JavaScript tree.
public final class RBush<Payload> {
    public typealias Projection = (RBushItem<Payload>) -> BoundingBox
    public typealias Comparison = (RBushItem<Payload>, RBushItem<Payload>) -> Double

    /// The input projection used at the C boundary. Remove before changing an
    /// indexed item's projected coordinates, then insert the same object again.
    public func toBBox(_ item: RBushItem<Payload>) -> BoundingBox {
        projection?(item) ?? item.boundingBox
    }
    public func compareMinX(_ lhs: RBushItem<Payload>, _ rhs: RBushItem<Payload>) -> Double {
        minXComparison?(lhs, rhs) ?? (toBBox(lhs).minX - toBBox(rhs).minX)
    }
    public func compareMinY(_ lhs: RBushItem<Payload>, _ rhs: RBushItem<Payload>) -> Double {
        minYComparison?(lhs, rhs) ?? (toBBox(lhs).minY - toBBox(rhs).minY)
    }
    /// Reset with nil. Changing a projection does not rebuild existing nodes.
    @discardableResult public func setToBBox(_ projection: Projection?) -> Self {
        self.projection = projection
        return self
    }
    /// Nil restores each original coordinate comparator. Hooks survive clear
    /// and fromJSON. Callbacks must not mutate or reenter this same tree.
    @discardableResult public func setComparators(compareMinX: Comparison? = nil, compareMinY: Comparison? = nil) -> Self {
        minXComparison = compareMinX; minYComparison = compareMinY
        return self
    }

    /// Structural snapshot, preserving child order, boxes and height exactly.
    /// Node containers have Swift value semantics; leaf items retain identity.
    public struct Node {
        public var boundingBox: BoundingBox
        public var height: Int
        public var leaf: Bool
        public var children: [Child]
        public init(boundingBox: BoundingBox, height: Int, leaf: Bool, children: [Child]) {
            self.boundingBox = boundingBox; self.height = height; self.leaf = leaf; self.children = children
        }
    }
    public indirect enum Child { case item(RBushItem<Payload>), node(Node) }

    private let handle: OpaquePointer
    private var projection: Projection?
    private var minXComparison: Comparison?
    private var minYComparison: Comparison?
    private var identities: [ObjectIdentifier: UInt64] = [:]
    private var items: [UInt64: RBushItem<Payload>] = [:]
    private var occurrences: [UInt64: Int] = [:]
    private var nextIdentity: UInt64 = 1
    public let maxEntries: Int
    public let minEntries: Int

    /// Customization closures are retained by Swift. Avoid strong captures of
    /// this tree, and do not mutate or reenter it from a customization callback.
    public init(maxEntries: Int = 9, toBBox: Projection? = nil, compareMinX: Comparison? = nil, compareMinY: Comparison? = nil) throws {
        guard let maximum = Int32(exactly: maxEntries) else { throw RBushError("maxEntries exceeds C ABI Int32 range") }
        var created: OpaquePointer?
        try check(rbush_create(maximum, &created))
        guard let created else { throw RBushError("C ABI did not create a tree") }
        var maxValue: Int32 = 0, minValue: Int32 = 0
        do { try check(rbush_get_capacity(created, &maxValue, &minValue)) }
        catch { rbush_destroy(created); throw error }
        handle = created; self.maxEntries = Int(maxValue); minEntries = Int(minValue)
        projection = toBBox; minXComparison = compareMinX; minYComparison = compareMinY
    }
    deinit { rbush_destroy(handle) }

    private func native(_ item: RBushItem<Payload>, identity: UInt64) -> rbush_item {
        let bounds = toBBox(item)
        return rbush_item(min_x: bounds.minX, min_y: bounds.minY,
                          max_x: bounds.maxX, max_y: bounds.maxY, identity: identity)
    }
    private func withComparators<Result>(_ operation: () throws -> Result) throws -> Result {
        guard minXComparison != nil || minYComparison != nil else { return try operation() }
        let candidates = items
        func bridge(_ comparison: Comparison?) -> ((UInt64, UInt64) -> Double)? {
            comparison.map { comparison in
                { lhs, rhs in
                    guard let lhs = candidates[lhs], let rhs = candidates[rhs] else {
                        preconditionFailure("Unknown comparison item identity")
                    }
                    return comparison(lhs, rhs)
                }
            }
        }
        var context = ComparisonContext(x: bridge(minXComparison), y: bridge(minYComparison))
        var x: rbush_compare_fn?, y: rbush_compare_fn?
        if minXComparison != nil { x = compareXCallback }
        if minYComparison != nil { y = compareYCallback }
        return try withUnsafeMutablePointer(to: &context) { pointer in
            try check(rbush_set_comparators(handle, x, y, pointer))
            // The handle remains live throughout this scope; clearing only the
            // callback pointers cannot fail. No raw stack address escapes.
            defer { _ = rbush_set_comparators(handle, nil, nil, nil) }
            return try operation()
        }
    }

    private func register(_ item: RBushItem<Payload>) -> UInt64 {
        let key = ObjectIdentifier(item)
        if let id = identities[key] { return id }
        let id = nextIdentity; nextIdentity += 1
        identities[key] = id; items[id] = item
        return id
    }
    private func forget(_ id: UInt64) {
        let remaining = (occurrences[id] ?? 0) - 1
        if remaining > 0 { occurrences[id] = remaining }
        else {
            if let item = items.removeValue(forKey: id) { identities.removeValue(forKey: ObjectIdentifier(item)) }
            occurrences.removeValue(forKey: id)
        }
    }
    @discardableResult public func insert(_ item: RBushItem<Payload>) throws -> Self {
        let id = register(item)
        try withComparators { try check(rbush_insert(handle, native(item, identity: id))) }
        occurrences[id, default: 0] += 1
        return self
    }
    @discardableResult public func load(_ values: [RBushItem<Payload>]) throws -> Self {
        let projected = values.map { native($0, identity: register($0)) }
        try withComparators {
            try projected.withUnsafeBufferPointer { try check(rbush_load(handle, $0.baseAddress, $0.count)) }
        }
        for value in projected { occurrences[value.identity, default: 0] += 1 }
        return self
    }
    /// Removes one occurrence. The optional equality function receives the
    /// requested item first and each candidate second, in original core order.
    @discardableResult public func remove(_ item: RBushItem<Payload>?, equals: ((RBushItem<Payload>, RBushItem<Payload>) -> Bool)? = nil) throws -> Self {
        guard let item else { return self }
        let id = identities[ObjectIdentifier(item)] ?? nextIdentity
        var removed: Int32 = 0, removedID: UInt64 = 0
        if let equals {
            let candidates = items
            var context = EqualityContext { candidate in
                guard let candidate = candidates[candidate] else { return false }
                return equals(item, candidate)
            }
            try withUnsafeMutablePointer(to: &context) { pointer in
                try check(rbush_remove_ex(handle, native(item, identity: id), equalityCallback, pointer, &removed, &removedID))
            }
        } else {
            try check(rbush_remove_ex(handle, native(item, identity: id), nil, nil, &removed, &removedID))
        }
        if removed != 0 { forget(removedID) }
        return self
    }
    @discardableResult public func clear() throws -> Self {
        try check(rbush_clear(handle))
        identities.removeAll(); items.removeAll(); occurrences.removeAll()
        return self
    }
    private func query(_ box: BoundingBox?) throws -> [RBushItem<Payload>] {
        var result: OpaquePointer?
        try check(box.map { rbush_search_result(handle, $0.native, &result) } ?? rbush_all_result(handle, &result))
        guard let result else { throw RBushError("C ABI did not create a query result") }
        defer { rbush_result_destroy(result) }
        let count = rbush_result_count(result)
        guard count > 0 else { return [] }
        guard let data = rbush_result_data(result) else { throw RBushError("C ABI returned an empty result buffer") }
        return try UnsafeBufferPointer(start: data, count: count).map { value in
            guard let item = items[value.identity] else { throw RBushError("Unknown item identity in C ABI result") }
            return item
        }
    }
    public func all() throws -> [RBushItem<Payload>] { try query(nil) }
    public func search(_ box: BoundingBox) throws -> [RBushItem<Payload>] { try query(box) }
    public func collides(_ box: BoundingBox) throws -> Bool {
        var result: Int32 = 0
        try check(rbush_collides(handle, box.native, &result))
        return result != 0
    }
    public func toJSON() throws -> Node {
        var nodeCount = 0, itemCount = 0, childCount = 0
        try check(rbush_export_counts(handle, &nodeCount, &itemCount, &childCount))
        var nodes = [rbush_node](repeating: rbush_node(), count: nodeCount)
        var values = [rbush_item](repeating: rbush_item(), count: itemCount)
        var children = [UInt64](repeating: 0, count: childCount)
        try nodes.withUnsafeMutableBufferPointer { n in
            try values.withUnsafeMutableBufferPointer { v in
                try children.withUnsafeMutableBufferPointer { c in
                    try check(rbush_to_json(handle, n.baseAddress, n.count, v.baseAddress, v.count, c.baseAddress, c.count))
                }
            }
        }
        func read(_ index: Int) throws -> Node {
            let node = nodes[index]
            let offset = Int(node.child_offset), count = Int(node.child_count)
            let entries: [Child] = try (offset..<(offset + count)).map { offset in
                if node.leaf != 0 {
                    guard let item = items[values[offset].identity] else { throw RBushError("Unknown snapshot item identity") }
                    return .item(item)
                }
                return .node(try read(Int(children[offset])))
            }
            return Node(boundingBox: .init(minX: node.min_x, minY: node.min_y, maxX: node.max_x, maxY: node.max_y),
                        height: Int(node.height), leaf: node.leaf != 0, children: entries)
        }
        return try read(0)
    }
    /// Imports the snapshot graph directly; never flattens and bulk rebuilds.
    @discardableResult public func fromJSON(_ root: Node) throws -> Self {
        var nodes: [rbush_node] = [], values: [rbush_item] = [], children: [UInt64] = []
        var newIDs: [ObjectIdentifier: UInt64] = [:], newItems: [UInt64: RBushItem<Payload>] = [:]
        var newCounts: [UInt64: Int] = [:]
        var next: UInt64 = 1
        func append(_ node: Node) throws -> UInt64 {
            guard let height = Int32(exactly: node.height) else { throw RBushError("Node height exceeds C ABI Int32 range") }
            let index = nodes.count
            nodes.append(rbush_node(min_x: node.boundingBox.minX, min_y: node.boundingBox.minY,
                max_x: node.boundingBox.maxX, max_y: node.boundingBox.maxY, child_offset: 0,
                child_count: UInt64(node.children.count), height: height, leaf: node.leaf ? 1 : 0))
            if node.leaf {
                nodes[index].child_offset = UInt64(values.count)
                for child in node.children {
                    guard case .item(let item) = child else { throw RBushError("Leaf node must contain items") }
                    let key = ObjectIdentifier(item)
                    let id: UInt64
                    if let existing = newIDs[key] { id = existing }
                    else { id = next; next += 1; newIDs[key] = id; newItems[id] = item }
                    newCounts[id, default: 0] += 1
                    values.append(native(item, identity: id))
                }
            } else {
                let offset = children.count
                nodes[index].child_offset = UInt64(offset)
                children.append(contentsOf: repeatElement(0, count: node.children.count))
                for (position, child) in node.children.enumerated() {
                    guard case .node(let node) = child else { throw RBushError("Internal node must contain nodes") }
                    children[offset + position] = try append(node)
                }
            }
            return UInt64(index)
        }
        _ = try append(root)
        try nodes.withUnsafeBufferPointer { n in
            try values.withUnsafeBufferPointer { v in
                try children.withUnsafeBufferPointer { c in
                    try check(rbush_from_json(handle, n.baseAddress, n.count, v.baseAddress, v.count, c.baseAddress, c.count))
                }
            }
        }
        identities = newIDs; items = newItems; occurrences = newCounts; nextIdentity = next
        return self
    }
}
