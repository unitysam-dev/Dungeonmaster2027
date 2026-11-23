import os,json,time,subprocess,urllib.request,urllib.error
from pathlib import Path
R=Path(__file__).parent.resolve();port=18905;url='http://127.0.0.1:'+str(port)
proc=subprocess.Popen(['python3','server.py'],cwd=R,env={**os.environ,'PORT':str(port)},stdout=subprocess.PIPE,stderr=subprocess.PIPE)
checks=[]
def request(path,data=None,token=None):
 req=urllib.request.Request(url+path,data=json.dumps(data).encode() if data is not None else None,headers={**({'Content-Type':'application/json'} if data is not None else {}),**({'Authorization':'Bearer '+token} if token else {})})
 try:
  with urllib.request.urlopen(req,timeout=5) as res:return res.status,json.loads(res.read())
 except urllib.error.HTTPError as e:return e.code,json.loads(e.read())
def ok(v,s):
 assert v,s;checks.append(s);print('PASS',s)
try:
 for _ in range(50):
  try:urllib.request.urlopen(url);break
  except OSError:time.sleep(.1)
 a=request('/api/join',{'party':0})[1];b=request('/api/join',{'party':1})[1]
 def state(t):return request('/api/state',token=t)[1]['world']
 seq=[0,0];tokens=[a['token'],b['token']]
 def act(p,action,args={}):
  seq[p]+=1;status,j=request('/api/action',{'action':action,'args':args,'sequence':seq[p]},tokens[p]);assert status==200,(status,j);return j['result']
 ok(a['partyId']==0 and b['partyId']==1 and tokens[0]!=tokens[1],'two actual HTTP clients independent token identities')
 w=state(tokens[0]);ok(w['map']==3 and w['depth']==3 and all(len(p['state']['champions'])==4 for p in w['parties']),'original Level 4 metadata and two full parties')
 ok(request('/api/join',{'party':0})[0]==409,'occupied party cannot be stolen')
 ok(request('/api/action',{'party':1,'action':'turn_left','sequence':1},tokens[0])[0]==403,'party impersonation rejected')
 ok(request('/api/state',token='bad')[0]==401,'invalid bearer rejected')
 ok(request('/api/action',{'action':'turn_left','sequence':1,'args':{'health':999}},tokens[0])[0]==400,'direct client health mutation rejected')
 act(0,'move_forward');time.sleep(.6);w=state(tokens[1]);ok(w['parties'][0]['state']['party']['x']==9 and w['parties'][1]['state']['party']['x']==10,'A moves while B unchanged; B sees movement')
 act(0,'move_forward');time.sleep(.3);ok(state(tokens[0])['parties'][0]['state']['party']['x']==9,'cannot move into other party')
 act(1,'turn_right');w=state(tokens[0]);ok(w['parties'][1]['state']['party']['facing']=='north' and w['parties'][0]['state']['party']['facing']=='east','B controls own turn without altering A')
 act(1,'turn_left');
 # Original screen upper action-hand slot (champion 0), then drop on A square.
 j=act(0,'mouse',{'x':30,'y':18});ok(j['ok'],'original hand mouse input accepted')
 w=state(tokens[0]);ok(w['parties'][0]['state']['leader_hand'] is not None,'positive original mouse pickup from action hand')
 name=w['parties'][0]['state']['leader_hand']['name'];act(0,'drop_object',{'cell':0});ok(state(tokens[0])['parties'][0]['state']['leader_hand'] is None,'original drop removes hand ownership')
 # B faces A from adjacent square; find original pile cell that contains shared object.
 w=state(tokens[1]);piles=w['parties'][1]['view']['piles'];print('PILES',json.dumps(piles))
 taken=False
 for cell in range(4):
  j=act(1,'take_object',{'cell':cell})
  if j['ok'] and state(tokens[1])['parties'][1]['state']['leader_hand']:
   taken=True;break
 ok(taken and state(tokens[1])['parties'][1]['state']['leader_hand']['name']==name,'other HTTP owner picks same live dropped object')
 ok(state(tokens[0])['parties'][0]['state']['leader_hand'] is None,'shared pickup never duplicates A hand')
 act(1,'drop_object',{'cell':0})
 # A throws its remaining ready torch using original click in upper dungeon view.
 act(0,'mouse',{'x':10,'y':18});before=sum(c['health'][0] for c in state(tokens[1])['parties'][1]['state']['champions']);act(0,'mouse',{'x':110,'y':60});time.sleep(3)
 after=sum(c['health'][0] for c in state(tokens[1])['parties'][1]['state']['champions']);ok(after<before,'live HTTP original thrown object friendly fire damages B')
 request('/api/leave',{},tokens[1]);w=state(tokens[0]);frozen=w['parties'][1]['state']['champions'];t=w['time'];time.sleep(1)
 w=state(tokens[0]);ok(not w['parties'][1]['connected'] and w['time']>t and w['parties'][1]['state']['champions']==frozen,'solo continues; withdrawn B resources preserved')
 request('/api/leave',{},tokens[0]);t=state(tokens[0])['time'];time.sleep(.8);ok(state(tokens[1])['time']==t,'nobody connected pauses original world time')
 request('/api/join',{'party':1,'token':tokens[1]});w=state(tokens[1]);ok(w['parties'][1]['connected'] and w['time']==t,'stable token rejoin does not reset shared world')
 ok(request('/api/action',{'action':'turn_left','sequence':1},tokens[1])[0]==409,'stale sequence rejected')
 print('SUMMARY',len(checks),'passed')
 (R/'evidence/http-results.json').write_text(json.dumps({'passed':checks},indent=2))
finally:
 proc.terminate()
 try:proc.wait(timeout=3)
 except subprocess.TimeoutExpired:proc.kill()
