// Exact deterministic geometry function from the earlier canvas comparison.
// Dataset selection is provided by the caller; no new random workload.
export function canvasFixture(n,dataset) {
function records(count,offset=0){
 const a=[];
 for(let i=0;i<count;i++){
  let x,y,w,h;
  if(dataset==='dense'){x=(i%3)*50000+Math.floor(i/3)%160*3-50000;y=Math.floor(i/480)%160*3-240;w=2;h=2;}
  else if(dataset==='sparse'){x=i%400*1100-220000;y=Math.floor(i/400)*1100-150000;w=8;h=6;}
  else if(dataset==='clustered_overlap'){
   let state=BigInt.asUintN(64,BigInt(i+1)*6364136223846793005n+1442695040888963407n);
   const next=()=>{state=BigInt.asUintN(64,state*6364136223846793005n+1n);return Number((state>>24n)%500n);};
   x=i%8*5000+next()-20000;y=Math.floor(i/8)%8*5000+next()-20000;w=32+next()/4;h=12+next()/8;
  }else{x=i%320*40-6400;y=Math.floor(i/320)*40-6400;w=i%100===0?20000:8;h=i%100===0?12000:5;}
  a.push({id:i+offset,minX:x,minY:y,maxX:x+w,maxY:y+h});
 }
 return a;
}
const base=records(n);
const all={id:n,minX:Infinity,minY:Infinity,maxX:-Infinity,maxY:-Infinity};
for(const r of base){all.minX=Math.min(all.minX,r.minX);all.minY=Math.min(all.minY,r.minY);all.maxX=Math.max(all.maxX,r.maxX);all.maxY=Math.max(all.maxY,r.maxY);}
const groups=[];
for(const radius of [2,8,24]){
 const queries=Array.from({length:240},(_,i)=>{
  const r=base[i*17%n],cx=(r.minX+r.maxX)/2,cy=(r.minY+r.maxY)/2;
  return {minX:cx-8.03125-radius,minY:cy-1.03125-radius,maxX:cx+8.03125+radius,maxY:cy+1.03125+radius};
 });
 for(const fraction of [0,.5])groups.push({radius,excludedFraction:fraction,excludedIDs:base.slice(0,Math.floor(n*fraction)).map(x=>x.id),queries});
}
return {n,dataset,items:base.concat(all),groups};
}
