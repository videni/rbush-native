import assert from 'node:assert/strict';
import {loadOriginalRBush} from './upstream.mjs';
const Original = await loadOriginalRBush();
import {random,uniform,cluster} from '../benchmarks/datasets.mjs';
const backend=process.argv[2] || 'wasm';
const adapter=await import(backend==='wasm' ? '../integrations/wasm/test-adapter.mjs' : '../tests/native-adapter.mjs');
const Port=adapter.default;
let heapBefore;
if(backend==='wasm') {
    heapBefore=adapter.module.HEAPU8.buffer.byteLength;
    const p=adapter.module._malloc(heapBefore+32*1024*1024);
    assert.ok(p,'explicit growth allocation');
    adapter.module._free(p);
    assert.ok(adapter.module.HEAPU8.buffer.byteLength>heapBefore,'test must exercise actual WASM memory growth');
}
for(const count of [5000,10000,50000,100000]) for(const distribution of ['uniform','cluster']) {
    const rng=random(42), data=distribution==='cluster'?cluster(count,rng):uniform(count,1,700,rng);
    const a=new Original(16).load(data), b=new Port(16).load(data);
    assert.deepEqual(b.toJSON(),a.toJSON(),`${backend} ${distribution} ${count} exact OMT structure including Floyd–Rivest partitions`);
    assert.deepEqual(b.all(),a.all(),`${distribution} ${count} ordered all identities`);
    for(let i=0;i<12;++i) {
        const x=rng()*650,y=rng()*650,q={minX:x,minY:y,maxX:x+50,maxY:y+50};
        assert.deepEqual(b.search(q),a.search(q),`${distribution} ${count} query ${i} ordered identities`);
        assert.equal(b.collides(q),a.collides(q));
    }
    b.dispose();
    console.log(`${backend}: ${distribution} ${count}: full tree, ordered all/search/collides match`);
}
if(backend==='wasm') console.log(`WASM memory growth exercised: ${heapBefore} -> ${adapter.module.HEAPU8.buffer.byteLength} bytes; adapter views remain valid`);
