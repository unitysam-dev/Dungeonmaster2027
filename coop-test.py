import os,json,time,subprocess,urllib.request,urllib.error,collections,copy,importlib.util
from pathlib import Path
R=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('gen',R/'generate-level.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
v=json.loads((R/'level.json').read_text());assert g.generate(v)==g.generate(copy.deepcopy(v))==(R/'level.generated.h').read_text()
print('PASS deterministic regenerated C import')
for label,mut in [('coordinates',lambda x:x['starts'][0].update(x=999)),('overlap',lambda x:x['starts'].__setitem__(1,x['starts'][0])),('tile',lambda x:x['rows'].__setitem__(1,'#X..#...#...#')),('boundary',lambda x:x['rows'].__setitem__(0,'.############')),('reachability',lambda x:x['rows'].__setitem__(7,'#############'))]:
 b=copy.deepcopy(v);mut(b)
 try:g.validate(b)
 except (AssertionError,KeyError,TypeError,ValueError):print('PASS rejected',label)
 else:raise AssertionError(label)
proc=subprocess.Popen(['python3','server.py'],cwd=R,env={**os.environ,'PORT':'18907'},stdout=open(R/'evidence/test-host.log','w'),stderr=subprocess.STDOUT)
url='http://127.0.0.1:18907';tokens=[];seq=[0,0]
def req(path,data=None,p=None):
 q=urllib.request.Request(url+path,data=None if data is None else json.dumps(data).encode(),headers={'Content-Type':'application/json',**({} if p is None else {'Authorization':'Bearer '+tokens[p]})})
 with urllib.request.urlopen(q,timeout=4) as r:return json.load(r)
def state(p=0):return req('/api/state',p=p)['world']
def act(p,a):
 seq[p]+=1;j=req('/api/action',{'sequence':seq[p],'action':a},p)['result'];assert j['ok'],j
 return j
def walk(p,target):
 for _ in range(100):
  w=state(p);s=w['parties'][p]['state']['party'];start=(s['x'],s['y'])
  if start==target:return w
  other=w['parties'][1-p]['state']['party'];occupied=(other['x'],other['y'])
  todo=collections.deque([(start,[])]);seen={start};path=None
  while todo:
   (x,y),steps=todo.popleft()
   if (x,y)==target:path=steps;break
   for dx,dy,d in [(0,-1,0),(1,0,1),(0,1,2),(-1,0,3)]:
    q=(x+dx,y+dy);a,b=q
    if q not in seen and q!=occupied and 0<=b<len(v['rows']) and 0<=a<len(v['rows'][0]) and (v['rows'][b][a]=='.' or v['rows'][b][a]=='D' and w['coop']['doorOpen']):seen.add(q);todo.append((q,steps+[d]))
  assert path,'no route'
  facing=['north','east','south','west'].index(s['facing']);d=path[0]
  while facing!=d:act(p,'turn_right');facing=(facing+1)%4
  # Authoritative cooldown, not position injection.
  while not state(p)['parties'][p]['state']['party']['can_move']:time.sleep(.08)
  act(p,'move_forward');time.sleep(.3)
 raise AssertionError('walk limit')
try:
 for _ in range(50):
  try:urllib.request.urlopen(url);break
  except OSError:time.sleep(.1)
 for p in range(2):tokens.append(req('/api/join',{'party':p})['token'])
 w=state();assert [(p['state']['party']['x'],p['state']['party']['y']) for p in w['parties']]==[(1,1),(7,1)];assert tokens[0]!=tokens[1];print('PASS two authenticated owners distinct configured starts, eight native champions')
 # B seal is owner-restricted: A alone visiting it still cannot open.
 walk(0,(2,2));assert state()['coop']['sealA'] and not state()['coop']['doorOpen'];print('PASS first seal alone stays closed')
 walk(0,(6,2));assert not state()['coop']['sealB'] and not state()['coop']['doorOpen'];print('PASS one party cannot activate both owner-specific seals')
 walk(0,(5,3));walk(1,(6,2));assert state(0)['coop']['doorOpen'] and state(1)['doors'][0]['state']==0;print('PASS second owner opens native shared door observed by both clients')
 walk(0,(5,8));assert state()['coop']['exitA'] and not state()['coop']['won'];print('PASS first exit alone is not victory')
 walk(1,(7,8));assert state(0)['coop']['won'] and state(1)['coop']['won'];print('PASS both exits shared victory through real native movement')
 (R/'evidence/coop-final-state.json').write_text(json.dumps(state()))
finally:
 proc.terminate();proc.wait(timeout=4)
