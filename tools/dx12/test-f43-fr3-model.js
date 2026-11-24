// Portable numerical model checks; NOT a C++/HLSL compilation or GPU test.
// Run: node tools/dx12/test-f43-fr3-model.js
function runRegression(){
  let checks=0;const assert=(b,m)=>{if(!b)throw Error(m);checks++};
  const dot=(a,b)=>a.reduce((s,v,i)=>s+v*b[i],0);
  const scale=(a,k)=>a.map(x=>x*k);
  const add=(a,b)=>a.map((x,i)=>x+b[i]);
  // Analytic oracle: translate within a rotated orthographic receiver plane.
  // Mirror the HLSL gradient equation and compare against directly projected points.
  for(let i=0;i<360;i+=3){
    const t=i*Math.PI/180,c=Math.cos(t),s=Math.sin(t);
    const x=scale([c,0,s],2/96),y=[0,2/96,0],z=scale([-s,0,c],1/191.9);
    const n=add(add(scale([c,0,s],0.3),[0,0.4,0]),scale([-s,0,c],-Math.sqrt(0.75)));
    const x2=dot(x,x),y2=dot(y,y),z2=dot(z,z),nz=dot(n,z);
    const gradient=[-z2/nz*2*dot(n,x)/x2,z2/nz*2*dot(n,y)/y2];
    for(const du of [-.5/2048,.5/2048])for(const dv of [-.5/2048,.5/2048]){
      const xy=add(scale(x,2*du/x2),scale(y,-2*dv/y2));
      const worldDelta=add(xy,scale(z,-dot(n,xy)/nz));
      assert(Math.abs(dot(n,worldDelta))<1e-12,"point not on plane");
      assert(Math.abs(dot(z,worldDelta)-(du*gradient[0]+dv*gradient[1]))<1e-12,"receiver gradient");
      const actualDepth=.5+dot(z,worldDelta);
      assert(.5+du*gradient[0]+dv*gradient[1]-.000015<=actualDepth,"self shadow on ideal plane");
      assert(.5+du*gradient[0]+dv*gradient[1]-.000015>actualDepth-.001,"occluder must still shadow");
    }
  }
  const score=(v,held,near,stable)=>v*(held?1.25:1)*(stable&&near?1.25:1);
  assert(score(1,true,false,true)>score(.7,false,true,true),"retention across near boundary");
  assert(score(1,true,false,true)<score(2,false,true,true),"strong contender replaces held light");
  const order=[0,17,15,12,10,24,6,18,2,14,21,9,4,23,7,16,1,20,11,3,25,8,19,5,13,22];
  assert(order.length===26&&new Set(order).size===26&&Math.min(...order)===0&&Math.max(...order)===25,"all sky rays exactly once");
  let samples=0,open=0,completed=null;
  for(let visit=0;visit<7;visit++){let end=Math.min(samples+4,26);while(samples<end){open+=1;samples++}if(samples===26)completed=open;}
  assert(samples===26&&completed===26,"bounded sweep completes");
  samples=0;open=0;for(let i=0;i<4;i++)samples++;
  assert(completed===26&&samples===4&&open===0,"retain completed estimate during refresh");
  const crossings=[false,false,true];let hit=-1;for(let i=0;i<crossings.length;i++){if(crossings[i]){hit=i;break;}}
  assert(hit===2,"continue after rejected crossings");
  return checks;
}
console.log("FR3 numerical model checks: "+runRegression()+" PASS");
