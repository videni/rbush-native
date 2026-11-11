import Original from './original.mjs';
import {WasmRBush,module} from '../../integrations/wasm/runtime.mjs';
import {random,uniform,cluster} from '../../benchmarks/datasets.mjs';
import {benchmark,microbenchmarks} from '../../benchmarks/benchmark.mjs';
import {begin,getResults} from '../../tests/browser/harness.mjs';

const $=id=>document.getElementById(id), canvas=$('canvas'),ctx=canvas.getContext('2d');
const colors=['#f40','#0b0','#37f'];
const ratio=devicePixelRatio||1;canvas.width=701*ratio;canvas.height=701*ratio;ctx.scale(ratio,ratio);
let tree,data=[],snapshot,rng,selection=[],queryBox,start;
const Tree=()=>$('engine').value==='wasm'?WasmRBush:Original;
const generate=n=>$('distribution').value==='cluster'?cluster(n,rng):uniform(n,1,700,rng);
function draw() {
    const rects=[];
    function visit(node,level) {
        rects.push([level?colors[(node.height-1)%3]:'gray',level?1/Math.pow(level,1.2):.2,node]);
        if(!node.leaf&&level!==6) for(const child of node.children) visit(child,level+1);
    }
    if(snapshot) visit(snapshot,0);
    ctx.clearRect(0,0,701,701);
    for(const [color,alpha,n] of rects.reverse()) {
        ctx.strokeStyle=color;ctx.globalAlpha=alpha;
        ctx.strokeRect(Math.round(n.minX),Math.round(n.minY),Math.round(n.maxX-n.minX),Math.round(n.maxY-n.minY));
    }
    ctx.globalAlpha=1;ctx.fillStyle='#f40';
    for(const b of selection) ctx.fillRect(b.minX-1,b.minY-1,Math.max(3,b.maxX-b.minX),Math.max(3,b.maxY-b.minY));
    if(queryBox) {ctx.strokeStyle='#222';ctx.setLineDash([4,3]);ctx.strokeRect(queryBox.minX,queryBox.minY,queryBox.maxX-queryBox.minX,queryBox.maxY-queryBox.minY);ctx.setLineDash([]);}
}
function status(operation,elapsed) {
    $('status').textContent=`${$('engine').value==='wasm'?'C++ / WASM':'Original JS'} · ${data.length.toLocaleString()} items · height ${snapshot.height} · ${operation}: ${elapsed.toFixed(2)} ms${queryBox?` · ${selection.length} hits`:''}`;
}
function mutate(name,fn) {const start=performance.now();fn();const elapsed=performance.now()-start;snapshot=tree.toJSON();selection=[];queryBox=null;draw();status(name,elapsed);}
function reset() {
    tree?.dispose?.();tree=new (Tree())($('distribution').value==='cluster'?16:10);data=[];rng=random(1);
    mutate('bulk load',()=>{data=generate(Number($('count').value));tree.load(data);});
}
function point(event) {const b=canvas.getBoundingClientRect();return {x:(event.clientX-b.left)*701/b.width,y:(event.clientY-b.top)*701/b.height};}
function search(a,b) {
    queryBox={minX:Math.min(a.x,b.x),minY:Math.min(a.y,b.y),maxX:Math.max(a.x,b.x)+1,maxY:Math.max(a.y,b.y)+1};
    const t=performance.now();selection=tree.search(queryBox);const elapsed=performance.now()-t;draw();status('search',elapsed);
}
canvas.addEventListener('pointerdown',event=>{start=point(event);canvas.setPointerCapture(event.pointerId);search(start,start);});
canvas.addEventListener('pointermove',event=>{if(start)search(start,point(event));});
canvas.addEventListener('pointerup',event=>{if(start)search(start,point(event));start=null;});
canvas.addEventListener('pointercancel',()=>{start=null;});
for(const id of ['engine','distribution','count']) $(id).addEventListener('change',reset);
$('reset').onclick=reset;
for(const button of document.querySelectorAll('[data-insert],[data-bulk]')) button.onclick=()=>mutate(button.textContent,()=>{
    const more=generate(Number(button.dataset.insert||button.dataset.bulk));
    if(button.dataset.bulk)tree.load(more);else for(const item of more)tree.insert(item);
    data=data.concat(more);
});
$('remove').onclick=()=>mutate('remove leftmost',()=>{data.sort(tree.compareMinX);for(const item of data.slice(0,10000))tree.remove(item);data.splice(0,10000);});
async function upstreamTests() {
    $('tests').disabled=true;$('test-results').replaceChildren();let passes=0,total=0;
    try {
        const response=await fetch('/__reference/rbush/test/test.js');
        if(!response.ok)throw new Error(`Pinned upstream tests unavailable: HTTP ${response.status}. Restart scripts/serve.py after fetching references.`);
        const source=await response.text();
        for(const [name,Base] of [['Original JS',Original],['C++ WASM',WasmRBush]]) {
            const trees=[];
            window.RBushUnderTest=class extends Base {constructor(...args){super(...args);trees.push(this);}};
            const harness=new URL('../../tests/browser/harness.mjs',import.meta.url).href;
            const adapted=source.replace("import RBush from '../index.js';",'const RBush = window.RBushUnderTest;')
                .replace("import test from 'node:test';",`import test from '${harness}';`)
                .replace("import assert from 'node:assert/strict';",`import {assert} from '${harness}';`);
            if(adapted===source)throw new Error('upstream imports changed');
            const url=URL.createObjectURL(new Blob([adapted],{type:'text/javascript'}));begin();
            try {await import(url);} finally {URL.revokeObjectURL(url);for(const tree of trees)tree.dispose?.();}
            for(const result of getResults()) {
                ++total;if(result.pass)++passes;
                const li=document.createElement('li');li.className=result.pass?'pass':'fail';
                li.textContent=`${name}: ${result.pass?'PASS':'FAIL'} ${result.name}${result.error?' — '+result.error:''}`;$('test-results').append(li);
            }
        }
        $('test-status').textContent=`${passes}/${total} passed`;$('test-status').className=passes===total?'pass':'fail';
        window.rbushTestResults={passes,total};
    } finally {$('tests').disabled=false;delete window.RBushUnderTest;}
}
$('tests').onclick=()=>upstreamTests().catch(showError);
async function runBenchmark(micro=false){
    $('bench').disabled=true;$('microbench').disabled=true;$('bench-status').textContent='Running…';$('bench-results').replaceChildren();
    try {
        const report=row=>{
            const tr=document.createElement('tr');for(const text of [row.engine,row.operation,`${row.ms.toFixed(2)} ms`,row.checksum??'—']){const td=document.createElement('td');td.textContent=text;tr.append(td);}$('bench-results').append(tr);
        };
        const engines=[['Original JS',Original],['C++ WASM',WasmRBush]];
        const rows=await (micro?microbenchmarks(engines,report):benchmark(engines,Number($('count').value),report));
        const a=rows.filter(r=>r.engine==='Original JS'&&r.checksum!==null),b=rows.filter(r=>r.engine==='C++ WASM'&&r.checksum!==null);
        if(a.some((row,i)=>row.checksum!==b[i].checksum))throw new Error('benchmark search counts differ');
        $('bench-status').textContent='Complete · query counts match';window.rbushBenchResults=rows;
    } catch(error){showError(error);$('bench-status').textContent='Failed';} finally {$('bench').disabled=false;$('microbench').disabled=false;}
}
$('bench').onclick=()=>runBenchmark();$('microbench').onclick=()=>runBenchmark(true);
function showError(error){$('error').textContent=error.stack||String(error);}
for(const button of document.querySelectorAll('button'))button.disabled=false;
$('loading').textContent=`WebAssembly loaded · Emscripten 4.0.20 · ${module.HEAPU8.buffer.byteLength.toLocaleString()} initial heap bytes`;
reset();
window.rbushDemo={get tree(){return tree;},get data(){return data;},module,upstreamTests,reset};
