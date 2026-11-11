import assert from 'node:assert/strict';
import {loadOriginalRBush} from './upstream.mjs';
const Original = await loadOriginalRBush();
import Port from '../integrations/wasm/test-adapter.mjs';

for(const count of [4,5,9,10]) {
    const points=Array.from({length:count},(_,i)=>({minX:i,minY:i,maxX:i,maxY:i}));
    assert.deepEqual(new Port(0).load(points).toJSON(),new Original(0).load(points).toJSON(),'explicit zero clamps to four');
}
const items=Array.from({length:24},(_,i)=>({minX:i%8,minY:Math.floor(i/8),maxX:i%8+1,maxY:Math.floor(i/8)+1,rank:i}));
let originalCalls=0,wasmCalls=0;
const a=new Original(4),b=new Port(4);
a.compareMinX=(x,y)=>{++originalCalls;return y.rank-x.rank;};
a.compareMinY=(x,y)=>{++originalCalls;return x.rank-y.rank;};
b.compareMinX=function(x,y){assert.equal(this,undefined,'comparison callback remains unbound');++wasmCalls;return y.rank-x.rank;};
b.compareMinY=function(x,y){assert.equal(this,undefined,'comparison callback remains unbound');++wasmCalls;return x.rank-y.rank;};
a.load(items);b.load(items);
assert.ok(originalCalls>0);assert.equal(wasmCalls,originalCalls,'custom comparator invocation count follows original native algorithm');
assert.deepEqual(b.toJSON(),a.toJSON(),'overflowing load uses actual custom comparators');
const more=items.map((x,i)=>({...x,rank:i+24,minX:x.minX+30,maxX:x.maxX+30}));
a.clear().load(more);b.clear().load(more);
assert.deepEqual(b.toJSON(),a.toJSON(),'custom comparators survive clear');
a.fromJSON(a.toJSON()).load(items);b.fromJSON(b.toJSON()).load(items);
assert.deepEqual(b.toJSON(),a.toJSON(),'custom comparators survive import');
assert.equal(wasmCalls,originalCalls,'comparator call counts stay equal through clear/import');
b.dispose();
console.log(`constructor and custom comparator regressions pass (${wasmCalls} matching comparator calls)`);

if(!global.gc)throw new Error('run node --expose-gc scripts/parity-adapter-regressions.mjs');
const tick=()=>new Promise(resolve=>setTimeout(resolve,0));
async function released(ref,label) {
    for(let i=0;i<12;++i) {await tick();global.gc();await tick();if(!ref.deref())return;}
    assert.fail(`payload retained after ${label}`);
}
const box=()=>({minX:0,minY:0,maxX:1,maxY:1,payload:new Uint8Array(4096)});
for(const operation of ['clear','remove','dispose','fromJSON','unknown remove','custom equality']) {
    const tree=new Port(4);let item=box();const ref=new WeakRef(item);
    if(operation!=='unknown remove')tree.insert(item);
    if(operation==='clear')tree.clear();
    else if(operation==='remove')tree.remove(item);
    else if(operation==='dispose')tree.dispose();
    else if(operation==='fromJSON')tree.fromJSON(new Original().toJSON());
    else if(operation==='unknown remove')tree.remove(item);
    else tree.remove({...item},()=>true);
    item=null;await released(ref,operation);tree.dispose();
}
{
    const tree=new Port(4);let item=box();const ref=new WeakRef(item);
    tree.insert(item).insert(item).remove(item);item=null;
    await tick();global.gc();await tick();assert.ok(ref.deref(),'one duplicate keeps its payload alive');
    tree.remove(tree.all()[0]);await released(ref,'last duplicate removal');tree.dispose();
}
console.log('WeakRef/GC: clear, remove, dispose, import, unknown removal, custom equality and duplicate lifetime pass');
