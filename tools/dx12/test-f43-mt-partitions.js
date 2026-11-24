// Partition/order model only; not a native GR or DX12 integration test.
// node tools/dx12/test-f43-mt-partitions.js
function model(){
 let n=0;const check=(x,msg)=>{if(!x)throw Error(msg);n++};
 const grain=256;
 for(const [ns,no,views] of [[0,0,[]],[1,0,[0]],[255,257,[0,3,8]],[256,256,[1]],[257,513,[0,1,2,3,4,5,6,7,8]],[13911,130,[0,2,3,4,5,6,7,8]]]){
  const sb=Math.ceil(ns/grain),ob=Math.ceil(no/grain),cnt=sb+ob+views.length;
  const serial=Array.from({length:cnt},(_,i)=>i);
  const orders=[serial,serial.slice().reverse(),serial.slice().sort((a,b)=>(a*7919%997)-(b*7919%997)||a-b)];
  function run(order){
   const st=Array(ns).fill(-1),obj=Array(no).fill(-1),sh=Array.from({length:9},()=>[[],[]]),done=Array(cnt).fill(0);
   for(const k of order){
    check(done[k]===0,"duplicate work item");done[k]++;
    if(k<sb){for(let i=k*grain;i<Math.min((k+1)*grain,ns);i++){check(st[i]===-1,"static overlap");st[i]=Number(i%7!==0)}}
    else if(k<sb+ob){let j=k-sb;for(let i=j*grain;i<Math.min((j+1)*grain,no);i++){check(obj[i]===-1,"object overlap");obj[i]=Number(i%5!==0)}}
    else {let p=views[k-sb-ob];for(let i=ns-1;i>=0;i--)if((i+p)%11===0)sh[p][i%2].push(i)}
   }
   check(done.every(v=>v===1)&&st.every(v=>v>=0)&&obj.every(v=>v>=0),"incomplete range");
   return JSON.stringify([st,obj,sh]);
  }
  let result=run(orders[0]);for(const o of orders.slice(1))check(result===run(o),"order mismatch");
 }
 return n;
}
console.log("MT partition model: "+model()+" PASS");
