import {random,uniform} from './datasets.mjs';
// Port of bench/perf.js's complete operation order. Shared input objects are
// generated once for both engines; wall times include the language adapter.
export async function benchmark(engines,count,report=()=>{}) {
    const rng=random(42),data=uniform(count,1,100,rng),data2=uniform(count,1,100,rng);
    const queries=[uniform(1000,100*Math.sqrt(.1),100,rng),uniform(1000,10,100,rng),uniform(1000,1,100,rng)];
    const rows=[];
    for(const [engine,Tree] of engines) {
        const tree=new Tree(16);
        const measure=async(name,fn)=> {
            await new Promise(resolve=>setTimeout(resolve,0));
            const start=performance.now(),result=fn();
            const row={engine,operation:name,ms:performance.now()-start,checksum:result??null};
            rows.push(row); report(row);
        };
        try {
            await measure('insert one by one',()=>{ for(const item of data) tree.insert(item); });
            for(let i=0;i<3;++i) await measure(`1000 searches ${['10%','1%','0.01%'][i]}`,()=>queries[i].reduce((n,q)=>n+tree.search(q).length,0));
            await measure('remove 1000 one by one',()=>{ for(const item of data.slice(0,1000)) tree.remove(item); });
            await measure(`bulk-insert ${count} more`,()=>tree.load(data2)&&undefined);
            for(let i=1;i<3;++i) await measure(`1000 searches ${['10%','1%','0.01%'][i]} after bulk`,()=>queries[i].reduce((n,q)=>n+tree.search(q).length,0));
            await measure('clear + bulk load',()=>tree.clear().load(data)&&undefined);
        } finally { tree.dispose?.(); }
    }
    return rows;
}

// Workloads from the four Benchmark.js suite files. Original counts/sizes;
// one sample each, since this panel does not claim statistical ops/sec results.
export async function microbenchmarks(engines,report=()=>{}) {
    const rng=random(42),insertData=uniform(100,1,100,rng),bulkData=uniform(10000,1,100,rng);
    const datasets=[false,true].map(bulk=>({bulk,data:uniform(10000,bulk?100:1,100,rng),queries:[uniform(1000,100*Math.sqrt(.1),100,rng),uniform(1000,10,100,rng),uniform(1000,1,100,rng)]}));
    const rows=[];
    for(const [engine,Tree] of engines) {
        const sample=async(operation,fn)=>{
            await new Promise(resolve=>setTimeout(resolve,0));
            const t=performance.now(),checksum=fn(),row={engine,operation,ms:performance.now()-t,checksum};
            rows.push(row);report(row);
        };
        await sample('insert.bench · insert 100',()=>{const t=new Tree(16);try{for(const i of insertData)t.insert(i);return insertData.length;}finally{t.dispose?.();}});
        await sample('bulk.bench · load 10000',()=>{const t=new Tree(16);try{t.load(bulkData);return bulkData.length;}finally{t.dispose?.();}});
        for(const d of datasets) {
            const t=new Tree(16);if(d.bulk)t.load(d.data);else for(const i of d.data)t.insert(i);
            try {for(let i=0;i<3;++i)await sample(`${d.bulk?'bulksearch':'search'}.bench · ${['10%','1%','0.01%'][i]}`,()=>d.queries[i].reduce((hits,q)=>hits+t.search(q).length,0));}
            finally{t.dispose?.();}
        }
    }
    return rows;
}
