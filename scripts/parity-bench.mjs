import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {loadOriginalRBush} from './upstream.mjs';
const Original = await loadOriginalRBush();
import Wasm from '../integrations/wasm/test-adapter.mjs';
import {benchmark,microbenchmarks} from '../benchmarks/benchmark.mjs';
const executable=process.env.RBUSH_BENCH || './build-native/rbush_bench';
function native(argument) {
    const result=spawnSync(executable,[String(argument)],{encoding:'utf8'});
    assert.equal(result.status,0,result.stderr||result.error?.message);
    return result.stdout.split('\n').filter(line=>line&&!line.startsWith('#')&&!line.startsWith('operation,')).map(line=>{
        const [operation,ms,checksum]=line.split(',');return {operation,ms:Number(ms),checksum:Number(checksum)};
    });
}
for(const count of [5000,10000,50000,100000]) {
    const rows=await benchmark([['js',Original],['wasm',Wasm]],count);
    const js=rows.filter(r=>r.engine==='js'&&r.checksum!==null),wasm=rows.filter(r=>r.engine==='wasm'&&r.checksum!==null),cpp=native(count).filter(r=>r.operation.startsWith('1000 searches'));
    assert.deepEqual(wasm.map(r=>r.checksum),js.map(r=>r.checksum),`${count} JS/WASM perf query checksums`);
    assert.deepEqual(cpp.map(r=>r.checksum),js.map(r=>r.checksum),`${count} native/JS perf query checksums`);
    console.log(`perf ${count}: native / WASM / pinned JS checksums match [${js.map(r=>r.checksum).join(', ')}]`);
}
const rows=await microbenchmarks([['js',Original],['wasm',Wasm]]);
const js=rows.filter(r=>r.engine==='js').map(r=>r.checksum),wasm=rows.filter(r=>r.engine==='wasm').map(r=>r.checksum);
assert.deepEqual(wasm,js);assert.deepEqual(native('suite').map(r=>r.checksum),js);
console.log(`all four original microbench workloads: native / WASM / pinned JS checksums match [${js.join(', ')}]`);
