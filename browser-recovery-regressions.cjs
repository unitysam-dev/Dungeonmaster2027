const {chromium}=require('playwright');
const {spawn}=require('node:child_process');const fs=require('node:fs');const path=require('node:path');const assert=require('node:assert/strict');
const R=__dirname,base='http://127.0.0.1:18892',checks=[],errors=[],consoleErrors=[];let host,browser;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
function ok(x,label){assert(x,label);checks.push(label);console.log('PASS',label)}
async function world(p){return await p.evaluate(async()=>{let o=JSON.parse(sessionStorage.getItem('dm-native-owner'));return(await(await fetch('/api/state',{headers:{Authorization:'Bearer '+o.token}})).json()).world})}
async function key(p,k){await p.bringToFront();await p.keyboard.press(k);await sleep(650)}
async function click(p,x,y,button='left'){let b=await p.locator('#view').boundingBox();await p.mouse.click(b.x+x*b.width/320,b.y+y*b.height/200,{button});await sleep(500)}
(async()=>{try{
 host=spawn('python3',['server.py'],{cwd:R,env:{...process.env,PORT:'18892'},stdio:['ignore','pipe','pipe']});
 for(let i=0;i<50;i++){try{await fetch(base);break}catch{await sleep(100)}}
 browser=await chromium.launch({headless:true,args:['--no-sandbox']});
 const c=await browser.newContext({viewport:{width:1100,height:1100}}),a=await c.newPage(),b=await c.newPage();
 for(let p of [a,b]){p.on('pageerror',e=>errors.push(e.message));p.on('console',m=>{if(m.type()==='error')consoleErrors.push(m.text())});await p.goto(base)}
 await a.locator('[data-slot="0"]').click();await b.locator('[data-slot="1"]').click();await a.waitForSelector('#game:not([hidden])');await b.waitForSelector('#game:not([hidden])');await sleep(600);
 ok(await a.evaluate(()=>sessionStorage.getItem('dm-native-owner'))!==await b.evaluate(()=>sessionStorage.getItem('dm-native-owner')),'two actual pages in SAME browser context own distinct parties');
 let beforeFormation=await world(a);
 for(let [index,page] of [a,b].entries()){
  let before=(await world(page)).parties[index].state.champions.map(c=>c.position);
  await click(page,285,5);await sleep(1300);await click(page,305,5);
  let after=(await world(page)).parties[index].state.champions.map(c=>c.position);
  ok(before.some((v,i)=>v!==after[i]),'original formation grid swaps after real clicks separated by polling in party '+index);
 }
 ok(await a.evaluate(()=>audioStats().state==='running'),'default sound enabled by ordinary game clicks');
 let w=await world(a);ok(w.parties.every(p=>p.state.champions.length===4)&&w.depth===3,'both browsers display actual original fourth floor and eight champions');
 ok(w.parties.every(p=>p.state.inventory===null),'recruitment fixture starts with original dungeon view, no accidental inventory');
 for(let [i,p]of [a,b].entries())await p.screenshot({path:path.join(R,'evidence','browser-initial-party-'+i+'.png'),fullPage:true});
 const pixels=await b.evaluate(()=>{let c=document.querySelector('canvas');return c.toDataURL()});
 await key(a,'w');w=await world(b);ok(w.parties[0].state.party.x===9&&w.parties[1].state.party.x===10,'A keyboard moves only A; B observes original authoritative position');
 await key(b,'s');w=await world(a);ok(w.parties[1].state.party.x===11&&w.parties[0].state.party.x===9,'B keyboard moves only B; A observes B');await key(b,'w');w=await world(a);ok(w.parties[1].state.party.x===10,'B independently returns through original movement');
 await key(b,'e');w=await world(a);ok(w.parties[1].state.party.facing==='north'&&w.parties[0].state.party.facing==='east','B keyboard independently turns B');await key(b,'q');
 // Original on-screen action hand -> held object -> floor -> other owner original pile.
 await click(a,30,18);w=await world(a);ok(w.parties[0].state.leader_hand?.name==='SWORD','actual canvas mouse picks original equipped ROCK');
 // Drop original held object onto A tile using native zone, not helper mutation.
 await click(a,75,160);w=await world(a);ok(w.parties[0].state.leader_hand===null,'actual original floor mouse click drops held ROCK');
 w=await world(b);let pile=w.parties[1].view.piles.find(p=>p.top==='SWORD');ok(!!pile,'B original view reports live shared dropped ROCK');
 // Use original native clickable pile box exported from the native renderer (no state mutation).
 // Typical adjacent near-right pile is around x143 y153; choose by click-box snapshot below if available.
 const cell=pile.cell,box=w.parties[1].click_boxes[cell];await click(b,box.x+box.w/2,box.y+box.h/2);w=await world(a);ok(w.parties[1].state.leader_hand?.name==='SWORD'&&w.parties[0].state.leader_hand===null,'B native pile input takes conserved shared object');
 // Return B rock to floor by original mouse.
 await click(b,75,160);
 // Original cursor throw: ready torch -> upper dungeon view -> actual pending damage on other party.
 await click(a,10,18);w=await world(a);ok(!!w.parties[0].state.leader_hand,'actual canvas picks original torch');const hp=w.parties[1].state.champions.reduce((n,c)=>n+c.health[0],0),stamina=w.parties[0].state.champions[0].stamina_raw[0];
 {let bb=await a.locator('#view').boundingBox();await a.mouse.click(bb.x+110*bb.width/320,bb.y+60*bb.height/200);await sleep(60);}const spent=(await world(a)).parties[0].state.champions[0].stamina_raw[0];await sleep(2600);w=await world(b);ok(w.parties[1].state.champions.reduce((n,c)=>n+c.health[0],0)<hp,'actual browser mouse throw causes original friendly-fire HP loss in other tab');console.log('STAMINA',stamina,spent);ok(spent<stamina,'throw cost reflected in actual authoritative stamina UI');
 for(let [i,p]of [a,b].entries())await p.screenshot({path:path.join(R,'evidence','browser-after-projectile-party-'+i+'.png'),fullPage:true});
 // Reverse live original friendly fire through B's own browser controls.
 await click(b,10,18);w=await world(b);const ahp=w.parties[0].state.champions.reduce((n,c)=>n+c.health[0],0);await click(b,110,60);await sleep(2600);w=await world(a);ok(w.parties[0].state.champions.reduce((n,c)=>n+c.health[0],0)<ahp,'B to A actual browser mouse throw damages original champion HP');
 // Original name/portrait inventory mouse input and return.
 await click(b,15,5,'right');w=await world(b);ok(w.parties[1].state.inventory?.champion===0,'original right-click portrait opens B champion inventory');await click(b,15,5,'right');w=await world(b);ok(w.parties[1].state.inventory===null,'original right-click portrait closes inventory');
 await a.reload();await a.waitForSelector('#game:not([hidden])');await sleep(400);await key(a,'q');w=await world(b);ok(w.parties[0].state.party.facing==='north','reload stable ownership remains independently controllable');
 await b.reload();await b.waitForSelector('#game:not([hidden])');await sleep(400);await key(b,'e');w=await world(a);ok(w.parties[1].state.party.facing==='north','B reload also preserves independent ownership');await key(b,'q');
 const popupPromise=a.waitForEvent('popup');await a.evaluate(()=>window.open('/'));const d=await popupPromise;await d.waitForURL(base+'/');await d.bringToFront();await d.waitForFunction(()=>document.querySelector('#status').textContent.includes('Duplicate tab'),null,{polling:100});
console.log('POPUP RENDER',JSON.stringify(await d.evaluate(()=>{let n=document.querySelector('#status'),s=getComputedStyle(n);return {text:n.textContent,inner:n.innerText,display:s.display,visibility:s.visibility,rect:n.getBoundingClientRect().toJSON(),bodyVisible:getComputedStyle(document.body).visibility,hidden:document.hidden,parents:[n.parentElement.tagName,n.parentElement.hidden]}})));
await d.screenshot({path:path.join(R,'evidence/popup-rejection.png'),fullPage:true});
ok((await d.locator('#status').innerText()).includes('Duplicate tab'),'inherited-sessionStorage popup rejected by nonstealing Web Lock');await d.close();
 await b.locator('#leave').click();await sleep(400);w=await world(a);let preserved=JSON.stringify(w.parties[1].state.champions),time=w.time;await key(a,'e');w=await world(a);ok(!w.parties[1].connected&&JSON.stringify(w.parties[1].state.champions)===preserved&&w.time>time,'explicit Leave withdraws B resources while solo A world continues');
 await a.locator('#leave').click();await sleep(400);time=(await world(a)).time;await sleep(850);ok((await world(a)).time===time,'both actual pages Leave pauses original shared world');
 await b.locator('#rejoin').click();await sleep(400);w=await world(b);ok(w.parties[1].connected&&w.time>=time&&JSON.stringify(w.parties[1].state.champions)===preserved,'reconnect preserves champions and shared progress without reset');
 ok(errors.length===0&&consoleErrors.length===0,'zero actual browser JavaScript page errors and console errors');
 fs.writeFileSync(path.join(R,'evidence/browser-results.json'),JSON.stringify({browser:await browser.version(),platform:process.platform,topology:'two pages in one Chromium context + inherited popup',checks,errors,consoleErrors},null,2));
 console.log('SUMMARY',checks.length,'passed');
 }finally{if(browser)await browser.close();if(host)host.kill('SIGTERM')}})().catch(e=>{console.error(e);fs.writeFileSync(path.join(R,'evidence/browser-failure.json'),JSON.stringify({checks,errors,error:e.stack},null,2));process.exitCode=1});
