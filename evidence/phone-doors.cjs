const {chromium}=require('playwright');
const {spawn}=require('node:child_process');const assert=require('node:assert/strict');const path=require('node:path');
const root=path.resolve(__dirname,'..'),url='http://127.0.0.1:18919';
const host=spawn('python3',['server.py'],{cwd:root,env:{...process.env,PORT:'18919'},stdio:['ignore','inherit','inherit']});
(async()=>{let browser;try{
 for(let n=0;n<50;n++){try{if((await fetch(url)).ok)break;}catch{} await new Promise(r=>setTimeout(r,100));}
 browser=await chromium.launch({headless:true,args:['--no-sandbox']});
 const parties=[];
 for(const [id,w,h] of [[0,390,844],[1,768,1024]]){
  const p=await browser.newPage({viewport:{width:w,height:h},isMobile:true,hasTouch:true});p.on('pageerror',e=>console.error('PAGEERROR',e.message));await p.goto(url);await p.waitForFunction(()=>typeof nativeAction==='function');await p.locator(`[data-slot="${id}"]`).tap();await p.waitForFunction(()=>window.lastWorld?.monsters?.length===15);parties.push(p);
 }
 const move=async(p,id,action,x,y,facing)=>{
  for(let n=0;n<15;n++){
   await p.evaluate(a=>nativeAction(a),action);
   const info=await p.locator('#status').textContent();
   if(/cooldown|sequence rejected/.test(info)){await p.waitForTimeout(280);continue;}
   try{await p.waitForFunction(({id,x,y,facing})=>{const a=lastWorld.parties[id].state.party;return a.x===x&&a.y===y&&(!facing||a.facing===facing)},{id,x,y,facing},{timeout:2200});return;}catch(e){console.error(id,action,x,y,facing,info,await p.evaluate(id=>lastWorld.parties[id].state.party,id));throw e;}
  } throw Error('persistent cooldown: '+action);
 };
 const a=parties[0],b=parties[1];
 await move(a,0,'move_right',1,4);await move(a,0,'move_right',1,5);await move(a,0,'move_forward',2,5);await move(a,0,'move_forward',3,5);await move(a,0,'turn_right',3,5,'south');
 await move(b,1,'turn_right',3,3,'north');await move(b,1,'turn_right',3,3,'east');
 for(let x=4;x<=8;x++)await move(b,1,'move_forward',x,3);
 await move(b,1,'move_right',8,4);await move(b,1,'move_right',8,5);await move(b,1,'move_forward',9,5);await move(b,1,'turn_right',9,5,'south');
 for(const [id,x,w,h] of [[0,3,390,844],[1,9,768,1024]]){
  const p=parties[id], other=parties[1-id];await p.locator('#fit-screen').tap();const bb=await p.locator('#view').boundingBox();assert(bb.x>=0&&bb.y>=0&&bb.x+bb.width<=w+1&&bb.y+bb.height<=h+1);
  const state=async(page,value)=>page.waitForFunction(([x,value])=>lastWorld.doors.some(d=>d.x===x&&d.y===6&&d.button&&d.state===value),[x,value]);
  const hit=await p.evaluate(id=>lastWorld.parties[id].click_boxes[5],id);assert(hit.x>=0&&hit.w>0&&hit.h>0);
  const tap=async()=>{const pt=await p.locator('#view').evaluate((el,hit)=>{const b=el.getBoundingClientRect();return [b.x+el.clientLeft+(hit.x+hit.w/2)*el.clientWidth/320,b.y+el.clientTop+(hit.y+hit.h/2)*el.clientHeight/200];},hit);await p.touchscreen.tap(...pt);};
  await state(p,4);await p.waitForFunction(()=>document.querySelector('#feature-guidance').textContent.startsWith('Door closed.'));await p.screenshot({path:`evidence/door-${x}-closed.png`});
  await tap();await state(p,0);await state(other,0);await p.waitForFunction(()=>document.querySelector('#feature-guidance').textContent.startsWith('Door open.'));await p.screenshot({path:`evidence/door-${x}-open.png`});
  await tap();await state(p,4);await state(other,4);console.log(`PASS ${w}x${h}: party walked to (${x},5), tapped original switch, door opened then closed for both parties`);
 }
}finally{if(browser)await browser.close();host.kill('SIGTERM');}})().catch(e=>{console.error(e);process.exitCode=1;});
