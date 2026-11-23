const {chromium}=require('playwright');
const {spawn}=require('node:child_process'),fs=require('node:fs'),assert=require('node:assert/strict');
const R=__dirname,base='http://127.0.0.1:18901',sleep=ms=>new Promise(r=>setTimeout(r,ms));let host,browser;
(async()=>{try{
host=spawn('python3',['server.py'],{cwd:R,env:{...process.env,PORT:'18901'},stdio:'ignore'});
for(let i=0;i<60;i++){try{await fetch(base);break}catch{await sleep(100)}}
browser=await chromium.launch({headless:true,args:['--no-sandbox']});
const context=await browser.newContext({viewport:{width:1100,height:1100}}),a=await context.newPage(),b=await context.newPage(),errors=[];
for(const[i,p]of[a,b].entries()){p.on('pageerror',e=>errors.push(e.message));await p.goto(base);await p.locator('[data-slot="'+i+'"]').click();await p.waitForSelector('#game:not([hidden])');}
await sleep(1400);await a.bringToFront();await a.keyboard.press('w');await sleep(1100);
const result=await b.evaluate(async()=>{
 const {drawPartyArt,projectParty}=await import('/party-art.js');const w=window.lastWorld,c=document.querySelector('#view'),actual=c.getContext('2d').getImageData(0,0,320,200).data;
 const off=document.createElement('canvas');off.width=320;off.height=200;const ctx=off.getContext('2d');const figures=drawPartyArt(ctx,w,1),px=ctx.getImageData(0,0,320,200).data;let opaque=0,match=0,outside=0;
 for(let i=0;i<320*200;i++){if(px[i*4+3]===255){opaque++;if(px[i*4]===actual[i*4]&&px[i*4+1]===actual[i*4+1]&&px[i*4+2]===actual[i*4+2])match++;const x=i%320,y=Math.floor(i/320);if(x>=224||y<33||y>=169)outside++;}}
 return {opaque,match,outside,figures:figures.map(f=>({portrait:f.portrait,view:f.view,pose:f.pose,depth:f.depth})),names:w.parties[0].state.champions.map(c=>c.name)};
});
assert(result.opaque>2500);assert(result.match/result.opaque>.98);assert.equal(result.outside,0);assert.equal(result.figures.length,4);assert(result.figures.every(f=>f.view==='front'));assert.deepEqual(result.figures.map(f=>f.portrait).sort((a,b)=>a-b),[1,10,13,23]);
await b.screenshot({path:R+'/evidence/parent-live-party.png',fullPage:true});
await a.evaluate(()=>nativeAction('choose_action',{champion:0,index:0}));await sleep(420);await b.screenshot({path:R+'/evidence/parent-live-attack.png',fullPage:true});
assert.equal(errors.length,0);fs.writeFileSync(R+'/evidence/parent-live-pixel-result.json',JSON.stringify({result,errors,platform:process.platform,topology:'two real pages one context',accepted:'actual canvas matches reviewed runtime artwork; original keyboard approach; clipping preserves UI'},null,2));console.log('PASS live canvas pixel match '+result.match+'/'+result.opaque+'; four source identities; zero UI overflow; zero browser errors');
}finally{if(browser)await browser.close();if(host){host.kill('SIGTERM');await new Promise(r=>host.once('exit',r));}}})().catch(e=>{console.error(e);process.exit(1)});
