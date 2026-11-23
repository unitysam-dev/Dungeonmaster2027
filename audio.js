// SPDX-License-Identifier: GPL-3.0-or-later
// Original signed8 samples; playback clock/period/filter adapted from pinned DMMac sound.c and ui/audio.c.
const raw=await fetch('/original-sounds.json');if(!raw.ok)throw Error('Original audio data missing');const A=await raw.json();
const bytes={};for(const [id,s]of Object.entries(A.samples))bytes[id]=Int8Array.from(atob(s.data),c=>c.charCodeAt(0));
let ctx,master,analyser,lastId=null;const voices=[];let played=0,lastSound=null,peak=0,error=null;
export async function unlockAudio(){try{if(!ctx){ctx=new AudioContext();master=ctx.createGain();analyser=ctx.createAnalyser();analyser.fftSize=2048;master.connect(analyser);analyser.connect(ctx.destination);}await ctx.resume();}catch(e){error=String(e);}}
export function audioStats(){if(analyser){const v=new Float32Array(analyser.fftSize);analyser.getFloatTimeDomainData(v);peak=Math.max(peak,...v.map(Math.abs));}return {state:ctx?.state??'locked',played,lastSound,lastId,peak,error};}
export function renderSample(id,outputRate,left=64,right=64){const s=A.samples[id],data=bytes[id];if(!s||!data)return null;const n=Math.ceil(data.length*outputRate*s.period/A.palClock),channels=[new Float32Array(n),new Float32Array(n)];for(let side=0;side<2;side++){const filter=[0,0,0],volume=(side?right:left)/128;for(let i=0;i<n;i++){let value=(data[Math.floor(i*A.palClock/(s.period*outputRate))]??0)/128*volume;for(let j=0;j<3;j++){const alpha=1/(1+outputRate/(2*Math.PI*(j?3300:4500)));filter[j]+=alpha*(value-filter[j]);value=filter[j];}channels[side][i]=value;}}return channels;}
function volumes(p,e){const dx=e.x-p.x,dy=e.y-p.y;let side,ahead;if(p.dir===0){side=dx;ahead=dy;}else if(p.dir===1){side=dy;ahead=-dx;}else if(p.dir===2){side=-dx;ahead=-dy;}else{side=-dy;ahead=dx;}if(Math.abs(side)>12||Math.abs(ahead)>12)return null;return [A.volumes[(ahead+12)*25+12-side],A.volumes[(ahead+12)*25+12+side]];}
function play(id,vol){if(ctx?.state!=='running')return;const data=renderSample(id,ctx.sampleRate,...vol);if(!data)return;const buffer=ctx.createBuffer(2,data[0].length,ctx.sampleRate);buffer.copyToChannel(data[0],0);buffer.copyToChannel(data[1],1);const source=ctx.createBufferSource();source.buffer=buffer;source.connect(master);while(voices.length>=2){try{voices.shift().stop();}catch{}}voices.push(source);source.onended=()=>{const at=voices.indexOf(source);if(at>=0)voices.splice(at,1);};source.start();played++;lastSound=id;}
export function acceptAudio(world,p){if(lastId===null){lastId=world.soundCursor??0;return;}for(const e of world.events??[]){if(e.type!=='sound'||e.id<=lastId)continue;lastId=e.id;if(!p.connected||e.map!==p.map)continue;const v=volumes(p,e);if(v)try{play(e.sound,v);}catch(e){error=String(e);document.querySelector("#status").textContent="Sound playback error: "+error;}}}
export function playSound(){} // Old own-command/log-tail triggers intentionally do nothing.

export function baselineAudio(cursor){lastId=cursor;}
export {volumes};
