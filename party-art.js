// GPL-3.0-or-later. Authored v4 art, source-bound equipment, authoritative poses.
import {ASSETS} from './art-assets.js';
const images=new Map();
if(typeof Image!=='undefined')for(const [k,v]of Object.entries(ASSETS)){const im=new Image();im.src=v;images.set(k,im);}
const dirs={north:0,east:1,south:2,west:3},vectors=[[0,-1],[1,0],[0,1],[-1,0]],cells=[[-.25,-.25],[.25,-.25],[.25,.25],[-.25,.25]],views=['back','left','front','right'];
export function poseFor(a,time){if(!a.serial)return 'idle';const weapon={23:10,1:18,10:25,13:34,9:10,0:18,2:25,18:34}[a.portrait];if(!a.equipment.some(e=>e.slot===1&&e.kind===5&&e.type===weapon))return 'idle';const dt=time-a.tick;return dt<0||dt>=5?'idle':['windup','strike','follow','recover','recover'][dt];}
export function blocked(s){return s===undefined||(s>>5)===0||((s>>5)===4&&(s&7)>0&&(s&7)<5)||((s>>5)===6&&!(s&4));}
export function projectParty(world,id){
 const me=world.parties[id],them=world.parties[1-id],own=me.state.party,q=them.state.party;
 if(!me.connected||!them.connected||own.dead||q.dead||me.state.inventory||own.map!==q.map)return [];
 const facing=dirs[own.facing],[fx,fy]=vectors[facing],dx=q.x-own.x,dy=q.y-own.y,z=dx*fx+dy*fy;
 if(z<1||z>4||dx*(-fy)+dy*fx!==0||me.ray.length<z||me.ray.slice(0,z).some(blocked))return [];
 const out=[];
 for(const [i,c] of them.state.champions.entries()){
  if(!c.alive)continue;const a=them.art?.[i];if(!a)continue;
  // Source champion.cell is absolute NW NE SE SW; rotate into viewer basis, not party order.
  const [x,y]=cells[a.cell&3],lateral=x*(-fy)+y*fx,depth=z-(x*fx+y*fy)*-1;
  const scale=(83/84)/depth,view=views[(facing-a.direction+4)%4],portrait=String(a.portrait).padStart(2,'0'),pose=poseFor(a,world.time),stem=portrait+'-'+view+'-'+pose;
  let behind=[],front=[],unsupported=[];
  for(const e of a.equipment){const key=stem+'-'+e.kind+'-'+e.type+'-'+e.slot;if(!ASSETS[key]){unsupported.push(e);continue;}
   const physical=(e.kind===5&&e.type===25)||e.slot===0?'L':'R';
   ((e.slot<2&&((view==='right'&&physical==='L')||(view==='left'&&physical==='R')))?behind:front).push(key);
  }
  out.push({i,portrait:a.portrait,view,pose,depth,scale,x:112+96*lateral/depth,foot:33+54+56/depth,layers:[...behind,'body/'+stem,...front],supported:!!ASSETS['body/'+stem],unsupported});
 }
 return out.sort((a,b)=>b.depth-a.depth||a.i-b.i);
}
export function drawPartyArt(ctx,world,id){const figures=projectParty(world,id);ctx.save();ctx.beginPath();ctx.rect(0,33,224,136);ctx.clip();ctx.imageSmoothingEnabled=false;
 for(const f of figures){const x=f.x-48*f.scale,y=f.foot-110*f.scale;
  if(f.supported){for(const k of f.layers){const im=images.get(k);if(im?.complete&&im.naturalWidth)ctx.drawImage(im,x,y,96*f.scale,128*f.scale);}}
  else{ctx.fillStyle='#777';ctx.fillRect(f.x-8*f.scale,f.foot-56*f.scale,16*f.scale,56*f.scale);ctx.fillStyle='#aaa';ctx.fillRect(f.x-6*f.scale,f.foot-66*f.scale,12*f.scale,10*f.scale);ctx.font='5px monospace';ctx.fillStyle='#fff';ctx.fillText('?',f.x-2,f.foot-32*f.scale);}
  // Explicit amber unknown-item marker, never paint a fabricated weapon.
  if(f.unsupported.length){ctx.fillStyle='#fa0';ctx.fillRect(f.x+10*f.scale,f.foot-38*f.scale,2,2);}
 }
 ctx.restore();return figures;
}
