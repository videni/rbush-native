import assert from 'node:assert/strict';
import {loadOriginalRBush} from './upstream.mjs';
const Original = await loadOriginalRBush();
const backend=process.argv[2] || 'native';
const {default:Port}=await import(backend==='wasm' ? '../integrations/wasm/test-adapter.mjs' : '../tests/native-adapter.mjs');
let checkpoints=0;
for(const seed of [1,17,613]) for(const max of [4,9,16]) {
    let state=seed;
    const random=()=>((state=Math.imul(state,1664525)+1013904223>>>0)/4294967296);
    const a=new Original(max), b=new Port(max), data=[];
    for(let i=0;i<350;++i) {
        const x=Math.floor(random()*40), y=Math.floor(random()*40), width=i%5===0?0:random()*5;
        data.push({minX:x,minY:y,maxX:x+width,maxY:y+width,id:i,group:i%7});
    }
    const verify=label=> {
        assert.deepEqual(b.toJSON(),a.toJSON(),`${backend} exact tree ${seed}/${max}: ${label}`);
        assert.deepEqual(b.all(),a.all(),`${backend} all order ${label}`);
        for(let i=0;i<4;++i) {
            const x=random()*40,y=random()*40,q={minX:x,minY:y,maxX:x+10,maxY:y+10};
            assert.deepEqual(b.search(q),a.search(q),`${backend} search identities/order ${label}`);
            assert.equal(b.collides(q),a.collides(q),`${backend} collides ${label}`);
        }
        ++checkpoints;
    };
    a.load(data.slice(0,170)); b.load(data.slice(0,170)); verify('bulk OMT');
    a.load(data.slice(170,175)); b.load(data.slice(170,175)); verify('small STLT');
    for(let i=175;i<225;++i) { a.insert(data[i]); b.insert(data[i]); } verify('split cascade');
    a.load(data.slice(225)); b.load(data.slice(225)); verify('merge');
    a.load(data.slice(0,20)); b.load(data.slice(0,20)); verify('duplicate references');
    for(let i=0;i<100;i+=3) { a.remove(data[i]); b.remove(data[i]); } verify('reference remove');
    const clone={...data[120]}; a.remove(clone); b.remove(clone); verify('clone identity no-op');
    a.remove(clone,(x,y)=>x.id===y.id); b.remove(clone,(x,y)=>x.id===y.id); verify('custom equality');
    const saved=a.toJSON(); a.clear().fromJSON(saved); b.clear().fromJSON(saved); verify('exact import');
    for(const item of a.all()) { a.remove(item); b.remove(item); } verify('condense to empty');
    b.dispose();
}
console.log(`${backend}: ${checkpoints} full-tree checkpoints; ordered identities/search/collides match pinned JS`);
