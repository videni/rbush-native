import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {performance} from 'node:perf_hooks';
import os from 'node:os';
import {loadOriginalRBush} from '../scripts/upstream.mjs';
import {canvasFixture} from './canvas-data.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);
const value=(key,fallback)=>args.includes(key)?args[args.indexOf(key)+1]:fallback;
const counts=value('--counts','5000,10000,50000,100000').split(',').map(Number);
const datasets=value('--datasets','dense,sparse,mixed_large,clustered_overlap').split(',');
const output=path.resolve(value('--output',path.join(root,'logs/canvas-comparison.json')));
const fixtureDir=path.join(path.dirname(output),'canvas-fixtures');await mkdir(fixtureDir,{recursive:true});
const Original=await loadOriginalRBush(),raw=[];
const priorPath=value('--prior-results',null),prior=priorPath?JSON.parse(await readFile(priorPath,'utf8')):null;
const intersects=(a,b)=>a.minX<=b.maxX&&a.maxX>=b.minX&&a.minY<=b.maxY&&a.maxY>=b.minY;
const hash=s=>createHash('sha256').update(s).digest('hex');
const fixtures=[];
for(const n of counts)for(const dataset of datasets){
 const data=canvasFixture(n,dataset),table=new Map(data.items.map(x=>[x.id,x]));
 const trees={insert_loop:new Original(),bulk_load:new Original().load(data.items)};
 for(const item of data.items)trees.insert_loop.insert(item);
 const lines=[`${n} ${data.groups.length}`,...data.items.map(x=>[x.id,x.minX,x.minY,x.maxX,x.maxY].join(' '))];
 for(const group of data.groups){
  const excluded=new Set(group.excludedIDs);
  group.expected=group.queries.map(q=>trees.insert_loop.search(q).filter(x=>x.id<n&&!excluded.has(x.id)).map(x=>x.id).sort((a,b)=>a-b));
  const total=group.expected.reduce((s,x)=>s+x.length,0);
  if(prior){const old=prior.filter(x=>x.N===n&&x.dataset===dataset&&x.world_radius===group.radius&&x.excluded_fraction===group.excludedFraction);assert(old.length>0);assert(old.every(x=>x.total_candidates===total),`${n}/${dataset}: previous canvas result changed`);}
  lines.push(`${group.radius} ${group.excludedFraction} ${group.excludedIDs.length} ${group.queries.length}`,group.excludedIDs.join(' '));
  for(let i=0;i<group.queries.length;i++){const q=group.queries[i],ids=group.expected[i];lines.push([q.minX,q.minY,q.maxX,q.maxY,ids.length,...ids].join(' '));}
 }
 const text=lines.join('\n')+'\n',json=JSON.stringify(data),name=`${dataset}-${n}`;
 const input=path.join(fixtureDir,name+'.txt');await writeFile(input,text);await writeFile(path.join(fixtureDir,name+'.json'),json);
 fixtures.push({name,input,sha256:hash(text),jsonSha256:hash(json)});
 // Both implementations read the same serialized values. Preparation is not timed.
 const shared=JSON.parse(json),owner=new Map(shared.items.map(x=>[x.id,x]));
 const measuredTrees={insert_loop:new Original(),bulk_load:new Original().load(shared.items)};
 for(const item of shared.items)measuredTrees.insert_loop.insert(item);
 for(const group of shared.groups){
  const excluded=new Set(group.excludedIDs),total=group.expected.reduce((s,x)=>s+x.length,0);
  for(const [algorithm,tree]of Object.entries(measuredTrees)){
   const candidate=q=>{const result=[];for(const id of tree.search(q).map(x=>x.id)){const record=owner.get(id);if(record.id<n&&intersects(record,q)&&!excluded.has(id))result.push(record);}return result;};
   for(let i=0;i<group.queries.length;i++)assert.deepEqual(candidate(group.queries[i]).map(x=>x.id).sort((a,b)=>a-b),group.expected[i]);
   const samples=[];
   for(let round=0;round<11;round++){
    let count=0;const t=performance.now();for(const q of group.queries)count+=candidate(q).length;const ms=performance.now()-t;assert.equal(count,total);if(round>=2)samples.push(ms);
   }
   samples.sort((a,b)=>a-b);raw.push({engine:'upstream_js',algorithm,N:n,dataset,world_radius:group.radius,excluded_fraction:group.excludedFraction,query_segments:240,total_candidates:total,loop_p50_ms:samples[4],loop_p95_ms:samples[8],samples:9,oracle_verified:true});
  }
 }
 const result=spawnSync(process.env.RBUSH_CANVAS_BENCH||path.join(root,'build-native/rbush_canvas_fixture'),[input],{cwd:root,encoding:'utf8',maxBuffer:16*1024*1024});
 assert.equal(result.status,0,result.stderr||result.error?.message);
 const native=result.stdout.trim().split('\n').map(x=>JSON.parse(x));
 assert.equal(native.length,12);
 for(const row of native){const expected=raw.find(x=>x.engine==='upstream_js'&&x.algorithm===row.algorithm&&x.N===n&&x.dataset===dataset&&x.world_radius===row.world_radius&&x.excluded_fraction===row.excluded_fraction);assert.equal(row.total_candidates,expected.total_candidates);raw.push({engine:'cpp_port',dataset,...row});}
 console.log(`same fixture ${name}: exact query identities match; sha256=${hash(text)}`);
 await writeFile(output,JSON.stringify({metadata:{scope:'same earlier canvas data/queries/radii/exclusions; no replacement workload',counts,datasets,node:process.version,cpu:os.cpus()[0]?.model,samples:9,warmup:2,querySegments:240,priorResults:priorPath,time:new Date().toISOString()},fixtures,rows:raw},null,2)+'\n');
}
console.log(`saved ${output}`);
