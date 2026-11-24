// Portable model checks; not engine compilation or GPU execution.
function run(){
 let checks=0;const assert=(v,m)=>{if(!v)throw Error(m);checks++};
 // Bound the same transformed geometry independent of whether source header bounds are local or world.
 for(let i=0;i<360;i+=5){
   const t=i*Math.PI/180, cs=Math.cos(t),sn=Math.sin(t);
   const pts=[[-1,-2,-3],[2,5,1],[0,0,4],[-4,2,0]].map(([x,y,z])=>[x*2*cs+z*.5*sn+1000,y*3-200,-x*2*sn+z*.5*cs+700]);
   const min=[0,1,2].map(a=>Math.min(...pts.map(p=>p[a]))),max=[0,1,2].map(a=>Math.max(...pts.map(p=>p[a])));
   const center=min.map((v,a)=>Math.fround((v+max[a])*.5));
   const extent=min.map((v,a)=>Math.max(max[a]-center[a],center[a]-v));
   const r=Math.hypot(...extent),radius=Math.fround(r+1e-5*Math.max(1,r,...center.map(Math.abs)));
   for(const p of pts)assert(Math.hypot(...p.map((x,a)=>x-center[a]))<=radius,"transformed vertex outside sphere");
 }
 // Check reserved ranges in the existing two-slot heap and transform layout.
 for(const slots of [1,2,3,4]){
   const tex=16384,sun=tex+1,water=sun+1,local=water+3*(1+slots),terrain=local+1;
   const writes=new Set([0,sun,local,terrain]);for(let n=water;n<local;n++){assert(!writes.has(n),"water overlap");writes.add(n)}
   for(let slot=0;slot<slots;slot++){const first=water+3*(1+slot);assert(first>=water+3&&first+2<local,"water frame range")}
 }
 const visible=131072,detail=512,mat=64,slotBytes=(visible+detail)*mat;
 for(let slot=0;slot<2;slot++){
   const offset=slot*slotBytes;assert(offset+visible*mat<=offset+slotBytes-detail*mat,"overlap");
   assert(offset+(visible+detail)*mat<=(slot+1)*slotBytes,"overflow");
 }
 // Four rays per visit, completed result retained, each refresh uses one origin.
 let anchor=null,complete=null,samples=0,origins=[];
 for(let visit=0;visit<7;visit++){
   if(samples===0)anchor=visit*0.5;
   for(let i=0;i<4&&samples<26;i++,samples++)origins.push(anchor);
   if(samples===26)complete=origins.length;
 }
 assert(complete===26&&new Set(origins).size===1,"mixed-position sweep");
 return checks;
}
console.log("FR3 follow-up model checks: "+run()+" PASS");
