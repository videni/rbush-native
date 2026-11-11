let results=[];
export function begin() { results=[]; }
export function getResults() { return results.slice(); }
export default function test(name,fn) {
    try { fn(); results.push({name,pass:true}); }
    catch(error) { results.push({name,pass:false,error:error.stack || String(error)}); }
}
function same(a,b,seen=new WeakMap()) {
    if(Object.is(a,b)) return true;
    if(typeof a!=='object'||!a||typeof b!=='object'||!b) return false;
    if(seen.get(a)===b) return true; seen.set(a,b);
    const ak=Object.keys(a),bk=Object.keys(b);
    return ak.length===bk.length && ak.every(key=>Object.hasOwn(b,key)&&same(a[key],b[key],seen));
}
function failure(message) { throw new Error(message || 'assertion failed'); }
export const assert={
    deepEqual:(a,b,message)=> { if(!same(a,b)) failure(message || 'deepEqual: tree or result differs'); },
    equal:(a,b,message)=> { if(!Object.is(a,b)) failure(message || `equal: ${a} !== ${b}`); },
    ok:(value,message)=> { if(!value) failure(message); },
    doesNotThrow:fn=>fn()
};
