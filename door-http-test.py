import os,json,time,subprocess,urllib.request,urllib.error
from pathlib import Path
R=Path(__file__).resolve().parent;url='http://127.0.0.1:18883';proc=subprocess.Popen(['python3','server.py'],cwd=R,env={**os.environ,'PORT':'18883'},stdout=subprocess.DEVNULL)
def req(path,data=None,token=None):
 r=urllib.request.Request(url+path,data=None if data is None else json.dumps(data).encode(),headers={'Content-Type':'application/json',**({'Authorization':'Bearer '+token} if token else {})})
 with urllib.request.urlopen(r,timeout=5) as res:return json.loads(res.read())
try:
 for _ in range(50):
  try:urllib.request.urlopen(url);break
  except OSError:time.sleep(.1)
 a=req('/api/join',{'party':0});b=req('/api/join',{'party':1});token=a['token'];seq=0
 req('/api/leave',{},b['token'])
 def state():return req('/api/state',token=token)['world']
 def act(action,args={}):
  global seq
  seq+=1;j=req('/api/action',{'action':action,'args':args,'sequence':seq},token);assert j['result']['ok'],j
 def facing():return ['north','east','south','west'].index(state()['parties'][0]['state']['party']['facing'])
 route=[1,1,1,2,2,2,3,2,2,3,2,2,2,3,2,3,3,3,0,3,3,3,2,3,3,2,2,1]
 steps=[]
 for d in route:
  while facing()!=d:act('turn_right')
  for _ in range(30):
   if state()['parties'][0]['state']['party']['can_move']:break
   time.sleep(.12)
  before=state()['parties'][0]['state']['party'];act('move_forward');after=state()['parties'][0]['state']['party'];assert (before['x'],before['y'])!=(after['x'],after['y']),(before,after)
  steps.append([after['x'],after['y']])
 while facing()!=2:act('turn_right')
 w=state();p=w['parties'][0];assert (p['state']['party']['x'],p['state']['party']['y'])==(1,12)
 before=next(d['state'] for d in w['doors'] if (d['x'],d['y'])==(1,13));box=p['click_boxes'][5];assert box['x']>=0
 act('mouse',{'x':int(box['x']+box['w']/2),'y':int(box['y']+box['h']/2)});time.sleep(2)
 w=state();after=next(d['state'] for d in w['doors'] if (d['x'],d['y'])==(1,13));assert before!=after,(before,after)
 req('/api/join',{'party':1,'token':b['token']});other=req('/api/state',token=b['token'])['world'];assert other['doors']==w['doors']
 result={'passed':['28 real HTTP original movement steps across original level','original rendered door button clicked through authenticated HTTP mouse input','original timed door animation changed state','reconnected B sees same original door record without reset'],'door':[1,13],'before':before,'after':after,'route':steps}
 (R/'evidence/door-http-results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
finally:proc.terminate();proc.wait(timeout=5)
