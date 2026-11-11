import {createRBushClass} from './facade.mjs';

export function createWasmRBush(module) {
    function check(status) { if (status) throw new Error(module.UTF8ToString(module._rbush_last_error())); }
    // Reacquire the heap for every access: allocations can grow WebAssembly.Memory.
    const view = () => new DataView(module.HEAPU8.buffer);
    const u32 = p => view().getUint32(p,true);
    const u64 = p => Number(view().getBigUint64(p,true));
    const put64 = (p,n) => view().setBigUint64(p,BigInt(n),true);
    const putBox = (p,b) => ['minX','minY','maxX','maxY'].forEach((key,i) => view().setFloat64(p+i*8,b[key],true));
    const getBox = p => Object.fromEntries(['minX','minY','maxX','maxY'].map((key,i) => [key,view().getFloat64(p+i*8,true)]));
    const putItem = (p,item) => { putBox(p,item); put64(p+32,item.identity); };
    function buffers(sizes,fn) {
        const pointers=sizes.map(n=>module._malloc(Math.max(1,n)));
        if(pointers.some(p=>!p)) { pointers.filter(Boolean).forEach(p=>module._free(p)); throw new Error('WASM allocation failed'); }
        try { return fn(...pointers); } finally { pointers.forEach(p=>module._free(p)); }
    }
    return createRBushClass(max => {
        const tree=module._rbw_create(max);
        if(!tree) throw new Error(module.UTF8ToString(module._rbush_last_error()));
        let disposed=false,compareX=0,compareY=0;
        function query(kind,box) {
            return buffers([4,32],(count,b) => {
                if(box) putBox(b,box);
                check(kind==='all' ? module._rbush_all_result(tree,count) : module._rbw_search_result(tree,b,count));
                const result=u32(count);
                try {
                    const n=module._rbush_result_count(result), p=module._rbush_result_data(result);
                    return Array.from({length:n},(_,i)=>u64(p+i*40+32));
                } finally { module._rbush_result_destroy(result); }
            });
        }
        return {
            setComparators:(x,y)=>{
                const nextX=x?module.addFunction((a,b)=>x(Number(a),Number(b)),'djji'):0;
                let nextY=0;
                try {
                    nextY=y?module.addFunction((a,b)=>y(Number(a),Number(b)),'djji'):0;
                    check(module._rbush_set_comparators(tree,nextX,nextY,0));
                } catch(error) {if(nextX)module.removeFunction(nextX);if(nextY)module.removeFunction(nextY);throw error;}
                if(compareX)module.removeFunction(compareX);if(compareY)module.removeFunction(compareY);
                compareX=nextX;compareY=nextY;
            },
            all:()=>query('all'), search:b=>query('search',b),
            collides:b=>buffers([32,4],(p,result)=> { putBox(p,b); check(module._rbw_collides(tree,p,result)); return Boolean(u32(result)); }),
            clear:()=>check(module._rbush_clear(tree)),
            insert:item=>buffers([40],p=> { putItem(p,item); check(module._rbw_insert(tree,p)); }),
            load:items=>buffers([items.length*40],p=> { items.forEach((item,i)=>putItem(p+i*40,item)); check(module._rbush_load(tree,p,items.length)); }),
            remove:(item,equals)=>buffers([40,4,8],(p,removed,identity)=> {
                putItem(p,item);
                const callback=equals ? module.addFunction((a,b)=>Number(equals(Number(a),Number(b))),'ijji') : 0;
                try {check(module._rbw_remove(tree,p,callback,removed,identity));return u32(removed)?u64(identity):0;} finally { if(callback) module.removeFunction(callback); }
            }),
            toJSON:()=>buffers([12],counts=> {
                check(module._rbush_export_counts(tree,counts,counts+4,counts+8));
                const nn=u32(counts), ni=u32(counts+4), nc=u32(counts+8);
                return buffers([nn*56,ni*40,nc*8],(nodes,items,indices)=> {
                    check(module._rbush_to_json(tree,nodes,nn,items,ni,indices,nc));
                    function visit(index) {
                        const p=nodes+index*56, offset=u64(p+32), count=u64(p+40), height=view().getInt32(p+48,true), leaf=Boolean(u32(p+52));
                        return {...getBox(p),height,leaf,children:Array.from({length:count},(_,i)=>leaf ? {identity:u64(items+(offset+i)*40+32)} : visit(u64(indices+(offset+i)*8)))};
                    }
                    return visit(0);
                });
            }),
            fromJSON:root=> {
                const nodes=[],items=[],indices=[];
                function visit(n) {
                    const index=nodes.length; nodes.push(null);
                    const childIds=n.leaf ? [] : n.children.map(visit);
                    const offset=n.leaf ? items.length : indices.length;
                    if(n.leaf) items.push(...n.children); else indices.push(...childIds);
                    nodes[index]={...n,offset}; return index;
                }
                visit(root);
                return buffers([nodes.length*56,items.length*40,indices.length*8],(np,ip,cp)=> {
                    nodes.forEach((n,i)=> { const p=np+i*56; putBox(p,n); put64(p+32,n.offset); put64(p+40,n.children.length); view().setInt32(p+48,n.height,true); view().setInt32(p+52,Number(n.leaf),true); });
                    items.forEach((item,i)=>putItem(ip+i*40,item)); indices.forEach((id,i)=>put64(cp+i*8,id));
                    check(module._rbush_from_json(tree,np,nodes.length,ip,items.length,cp,indices.length));
                });
            },
            dispose:()=> { if(!disposed) {module._rbush_destroy(tree);if(compareX)module.removeFunction(compareX);if(compareY)module.removeFunction(compareY);disposed=true;} }
        };
    });
}
