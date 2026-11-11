// Original randBox / cluster formulas, with a reproducible RNG replacing Math.random.
export function random(seed=1) {
    let state=seed>>>0;
    return ()=>((state=(Math.imul(state,1664525)+1013904223)>>>0)/4294967296);
}
export function uniform(n,size=1,width=100,rng=Math.random) {
    const data=[];
    for(let i=0;i<n;++i) {
        const x=rng()*(width-size),y=rng()*(width-size);
        data.push({minX:x,minY:y,maxX:x+size*rng(),maxY:y+size*rng()});
    }
    return data;
}
export function cluster(n,rng=Math.random) {
    const data=[], W=700, M=30, R=100;
    for(let i=0;i<M;++i) {
        const point={x:R+rng()*(W-2*R),y:R+rng()*(W-2*R)};
        const count=Math.min(Math.ceil(n/M),n-data.length);
        for(let j=0;j<count;++j) {
            const x=point.x-R+2*R*(rng()+rng()+rng())/3;
            const y=point.y-R+2*R*(rng()+rng()+rng())/3;
            data.push({minX:x,minY:y,maxX:x+rng(),maxY:y+rng(),item:true});
        }
    }
    return data;
}
