// A language adapter, not a tree: every operation delegates to the native backend.
// Object references stay in JS; native identity values refer back to this map.
export function createRBushClass(makeBackend) {
    return class RBush {
        #backend; #objects = new Map(); #identities = new WeakMap(); #counts = new Map(); #next = 1;
        #compareX; #compareY; #disposed=false;
        constructor(maxEntries = 9) {
            this.#backend = makeBackend(Math.max(4, maxEntries));
            // Upstream exposes data as an enumerable own property. Preserve that
            // so its deepEqual(tree, tree) assertions compare actual tree state.
            Object.defineProperty(this, 'data', {enumerable:true, get:()=>this.toJSON()});
        }
        #item(item) {
            let identity = this.#identities.get(item);
            if (!identity) { identity = this.#next++; this.#identities.set(item, identity); }
            this.#objects.set(identity, item);
            const b = this.toBBox(item);
            return {identity, minX:b.minX, minY:b.minY, maxX:b.maxX, maxY:b.maxY};
        }
        #alive() { if(this.#disposed) throw new Error('RBush has been disposed'); }
        #retain(id) { this.#counts.set(id,(this.#counts.get(id)||0)+1); }
        #syncComparators() {
            const x=this.compareMinX,y=this.compareMinY;
            if(x===this.#compareX&&y===this.#compareY)return;
            // Upstream passes comparators as unbound callbacks to selection/sort.
            this.#backend.setComparators?.(x===RBush.prototype.compareMinX?null:(a,b)=>x(this.#objects.get(a),this.#objects.get(b)),y===RBush.prototype.compareMinY?null:(a,b)=>y(this.#objects.get(a),this.#objects.get(b)));
            this.#compareX=x;this.#compareY=y;
        }
        all() { this.#alive();return this.#backend.all().map(id => this.#objects.get(id)); }
        search(box) { this.#alive();return this.#backend.search(box).map(id => this.#objects.get(id)); }
        collides(box) { this.#alive();return this.#backend.collides(box); }
        clear() { this.#alive();this.#backend.clear();this.#objects.clear();this.#counts.clear();return this; }
        insert(item) {
            this.#alive();
            if(item) {const native=this.#item(item);this.#syncComparators();this.#backend.insert(native);this.#retain(native.identity);}
            return this;
        }
        load(items) {
            this.#alive();
            if(items?.length) {const native=items.map(item=>this.#item(item));this.#syncComparators();this.#backend.load(native);for(const item of native)this.#retain(item.identity);}
            return this;
        }
        remove(item, equals) {
            this.#alive();
            if(item) {
                const requested=this.#item(item);
                try {
                    const removed=this.#backend.remove(requested,equals&&((a,b)=>equals(this.#objects.get(a),this.#objects.get(b))));
                    if(removed) {
                        const count=this.#counts.get(removed)-1;
                        if(count) this.#counts.set(removed,count);else {this.#counts.delete(removed);this.#objects.delete(removed);}
                    }
                } finally {if(!this.#counts.has(requested.identity))this.#objects.delete(requested.identity);}
            }
            return this;
        }
        toBBox(item) { return item; }
        compareMinX(a,b) { return a.minX-b.minX; }
        compareMinY(a,b) { return a.minY-b.minY; }
        toJSON() {
            this.#alive();
            const visit = node => ({...node, children:node.children.map(child => node.leaf ? this.#objects.get(child.identity) : visit(child))});
            return visit(this.#backend.toJSON());
        }
        fromJSON(node) {
            this.#alive();const objects=new Map(),counts=new Map();
            const visit = n => ({...n, children:n.children.map(child => {
                if(!n.leaf)return visit(child);
                const native=this.#item(child);objects.set(native.identity,child);counts.set(native.identity,(counts.get(native.identity)||0)+1);return native;
            })});
            this.#backend.fromJSON(visit(node));this.#objects=objects;this.#counts=counts;return this;
        }
        dispose() { if(!this.#disposed){this.#backend.dispose?.();this.#objects.clear();this.#counts.clear();this.#disposed=true;} }
    };
}
