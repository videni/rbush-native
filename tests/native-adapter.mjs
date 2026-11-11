import {spawnSync} from 'node:child_process';
import {createRBushClass} from '../integrations/wasm/facade.mjs';
const bbox = b => `${b.minX} ${b.minY} ${b.maxX} ${b.maxY}`;
const item = b => `${b.identity} ${bbox(b)}`;
export default createRBushClass(max => {
    let operations = `C ${max}\n`;
    let compareX,compareY;
    const ids=new Set(),occurrences=new Map();
    const retain=id=>{ids.add(id);occurrences.set(id,(occurrences.get(id)||0)+1);};
    function comparatorTable() {
        if(!compareX&&!compareY)return;
        const pairs=[];
        for(const a of ids)for(const b of ids)pairs.push(`${a} ${b} ${compareX?compareX(a,b):0} ${compareY?compareY(a,b):0}`);
        operations+=`P ${pairs.length} ${Number(Boolean(compareX))} ${Number(Boolean(compareY))}\n${pairs.join('\n')}\n`;
    }
    function query(op) {
        const result = spawnSync(process.env.RBUSH_PARITY_CLI || './build-native/rbush_parity_cli', [], {input:operations + op + '\n',encoding:'utf8',maxBuffer:64*1024*1024});
        if (result.status !== 0) throw new Error(result.stderr || result.error?.message || 'native parity transport failed');
        return JSON.parse(result.stdout.trim().split('\n').at(-1), (_,v) => v === 'Infinity' ? Infinity : v === '-Infinity' ? -Infinity : v === 'NaN' ? NaN : v);
    }
    return {
        setComparators:(x,y)=>{compareX=x;compareY=y;if(!x&&!y)operations+='P 0 0 0\n';},
        all:() => query('A'), search:b => query(`S ${bbox(b)}`), collides:b => query(`X ${bbox(b)}`),
        clear:() => {operations+='Z\n';ids.clear();occurrences.clear();},
        insert:b => {retain(b.identity);comparatorTable();operations += `I ${item(b)}\n`;},
        load:items => {items.forEach(b=>retain(b.identity));comparatorTable();operations += `L ${items.length}\n${items.map(item).join('\n')}\n`;},
        remove:(b,equals) => {
            const matches = equals ? query('A').filter(id => equals(b.identity,id)) : undefined;
            const op=`R ${item(b)} ${matches ? `${matches.length} ${matches.join(' ')}` : -1}`;
            const removed=query(op);operations+=op+'\n';
            if(removed){const count=occurrences.get(removed)-1;if(count)occurrences.set(removed,count);else{occurrences.delete(removed);ids.delete(removed);}}
            return removed;
        },
        toJSON:() => query('E'),
        fromJSON:root => {
            const nodes=[], items=[], children=[];
            const visit = n => {
                const index=nodes.length; nodes.push(null);
                const ids=n.leaf ? [] : n.children.map(visit);
                const offset=n.leaf ? items.length : children.length;
                if(n.leaf) items.push(...n.children); else children.push(...ids);
                nodes[index]=`${bbox(n)} ${offset} ${n.children.length} ${n.height} ${Number(n.leaf)}`;
                return index;
            };
            visit(root);
            ids.clear();occurrences.clear();items.forEach(b=>retain(b.identity));
            operations += `F ${nodes.length} ${items.length} ${children.length}\n${nodes.join('\n')}\n${items.map(item).join('\n')}\n${children.join(' ')}\n`;
        }
    };
});
