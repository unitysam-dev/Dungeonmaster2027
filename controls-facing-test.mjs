import assert from 'node:assert/strict';import {projectParty} from './party-art.js';
const names=['north','east','south','west'],v=[[0,-1],[1,0],[0,1],[-1,0]];
let cases=0;for(let observer=0;observer<4;observer++)for(let target=0;target<4;target++){
const [x,y]=v[observer];const w={time:0,parties:[{connected:true,ray:[32,32,32,32],state:{party:{x:0,y:0,map:3,facing:names[observer]},champions:[]}},{connected:true,art:[{portrait:23,cell:target,direction:target,serial:0,equipment:[{slot:0,kind:5,type:10},{slot:1,kind:5,type:10}]}],state:{party:{x,y,map:3,facing:names[target]},champions:[{alive:true}]}}]};
const f=projectParty(w,0)[0],expected=['back','right','front','left'][(target-observer+4)%4];assert.equal(f.view,expected);if(expected==='right'){assert(f.layers[0].endsWith('-0'));assert(f.layers[1].startsWith('body/'));}if(expected==='left'){assert(f.layers[0].endsWith('-1'));assert(f.layers[1].startsWith('body/'));}cases++;}
console.log('PASS '+cases+' observer/target direction pairs and side-view physical hand occlusion');
