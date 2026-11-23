#!/usr/bin/env python3
"""Local original-engine host. Python standard library only. One engine and serialized commands."""
import os,json,time,secrets,threading,subprocess,signal,atexit
from pathlib import Path
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
ROOT=Path(__file__).resolve().parent
(ROOT/'evidence').mkdir(exist_ok=True)
DATA=Path(os.environ.get('DM_DATA',str(ROOT/'private-game'))).resolve()
PORT=int(os.environ.get('PORT','18906'))
class World:
 def __init__(self):
  self.lock=threading.RLock(); self.owners=[None,None]; self.last=[0,0];self.seq=[0,0];self.connected=[False,False]
  self.child=subprocess.Popen([str(ROOT/'bridge'),str(DATA)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1,env={**os.environ,'LD_LIBRARY_PATH':str(ROOT/'engine/build')})
  self.state=self.call({'cmd':'state'});self.running=True
  threading.Thread(target=self.clock,daemon=True).start()
 def call(self,req):
  with self.lock:
   self.child.stdin.write(json.dumps(req,separators=(',',':'))+'\n');self.child.stdin.flush()
   line=self.child.stdout.readline()
   if not line: raise RuntimeError('native engine stopped')
   result=json.loads(line)
   if result.get('ok'):self.state=result
   return result
 def clock(self):
  while self.running:
   time.sleep(.26)
   with self.lock:
    for p in range(2):
     if self.connected[p] and time.monotonic()-self.last[p]>15:
      self.call({'cmd':'leave','party':p});self.connected[p]=False
    if any(self.connected):self.call({'cmd':'tick'})
 def auth(self,h):
  token=h.headers.get('Authorization','').removeprefix('Bearer ')
  if token not in self.owners or not token:raise PermissionError('ownership required')
  return self.owners.index(token)
W=World()
def cleanup():
 W.running=False
 if W.child.poll() is None:
  W.child.terminate()
  try:W.child.wait(timeout=3)
  except subprocess.TimeoutExpired:W.child.kill();W.child.wait()
atexit.register(cleanup)
def terminate(*args):raise SystemExit(0)
signal.signal(signal.SIGTERM,terminate)

class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def valid_host(self):
  return self.headers.get('Host','') in {'127.0.0.1:'+str(PORT),'localhost:'+str(PORT)}
 def reply(self,obj,status=200,mime='application/json'):
  data=obj if isinstance(obj,bytes) else json.dumps(obj).encode();self.send_response(status);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.end_headers();self.wfile.write(data)
 def do_GET(self):
  if not self.valid_host():return self.reply({'error':'host rejected'},403)
  try:
   if self.path=='/':return self.reply((ROOT/'index.html').read_bytes(),mime='text/html; charset=utf-8')
   if self.path=='/client.js':return self.reply((ROOT/'client.js').read_bytes(),mime='text/javascript')
   if self.path=='/audio.js':return self.reply((ROOT/'audio.js').read_bytes(),mime='text/javascript')
   if self.path=='/original-sounds.json':
    samples=ROOT/'original-sounds.json'
    if not samples.is_file():return self.reply({'error':'Optional sound samples not installed'},404)
    return self.reply(samples.read_bytes(),mime='application/json')
   if self.path in ('/art-assets.js','/party-art.js'):return self.reply((ROOT/self.path[1:]).read_bytes(),mime='text/javascript')
   if self.path=='/party-marker.js':return self.reply((ROOT/'party-marker.js').read_bytes(),mime='text/javascript')
   if self.path=='/favicon.ico':return self.reply(b'',204,'image/x-icon')
   with W.lock:
    p=W.auth(self)
    if self.path=='/api/state':
     return self.reply({'partyId':p,'sequence':W.seq[p],'world':W.state})
    if self.path.startswith('/api/frame'):
     path=ROOT/'evidence'/('live-'+str(p)+'.png');W.call({'cmd':'frame','party':p,'path':str(path)})
     return self.reply(path.read_bytes(),mime='image/png')
   self.reply({'error':'not found'},404)
  except PermissionError as e:self.reply({'error':str(e)},401)
  except Exception as e:self.reply({'error':str(e)},500)
 def do_POST(self):
  if not self.valid_host():return self.reply({'error':'host rejected'},403)
  try:
   origin=self.headers.get('Origin')
   if origin and origin!='http://'+self.headers.get('Host',''):return self.reply({'error':'origin rejected'},403)
   n=int(self.headers.get('Content-Length','0'))
   if n<1 or n>2048:return self.reply({'error':'body size'},413)
   data=json.loads(self.rfile.read(n))
   if not isinstance(data,dict):raise ValueError('object required')
   if any(k not in {'party','token','sequence','action','args'} for k in data):raise ValueError('unknown request field')
   with W.lock:
    if self.path=='/api/join':
     p=data.get('party');token=data.get('token')
     if type(p)!=int or p not in (0,1):raise ValueError('party must be 0 or 1')
     if W.owners[p] is not None and not secrets.compare_digest(token or '',W.owners[p]):return self.reply({'error':'party already owned'},409)
     if W.owners[p] is None:W.owners[p]=secrets.token_urlsafe(32)
     W.last[p]=time.monotonic();W.connected[p]=True;W.call({'cmd':'connect','party':p})
     return self.reply({'partyId':p,'token':W.owners[p],'sequence':W.seq[p]})
    p=W.auth(self)
    if 'party' in data and data['party']!=p:return self.reply({'error':'party impersonation'},403)
    if self.path=='/api/heartbeat':
     W.last[p]=time.monotonic()
     if not W.connected[p]:W.call({'cmd':'connect','party':p});W.connected[p]=True
     return self.reply({'ok':True})
    if self.path=='/api/leave':
     W.call({'cmd':'leave','party':p});W.connected[p]=False;return self.reply({'ok':True})
    if self.path!='/api/action':return self.reply({'error':'not found'},404)
    if not W.connected[p]:return self.reply({'error':'party offline'},409)
    if type(data.get('sequence'))!=int or data['sequence']!=W.seq[p]+1:return self.reply({'error':'sequence rejected','sequence':W.seq[p]},409)
    allowed={'move_forward','move_back','move_left','move_right','turn_left','turn_right','mouse','take_object','drop_object','click_wall_ahead','open_inventory','close_inventory','put_in_slot','take_from_slot','eat','drink','choose_action','pass','set_caster','add_rune','delete_rune','cast','set_leader','look'}
    if data.get('action') not in allowed:raise ValueError('action unavailable')
    args=data.get('args',{})
    if not isinstance(args,dict) or any(k not in {'x','y','button','phase','cell','champion','slot','index','symbol','choice'} for k in args):raise ValueError('invalid arguments')
    if any(type(v)==int and not -32768<=v<=32767 for v in args.values()):raise ValueError('integer argument out of range')
    if any(type(v) not in (int,str) or (isinstance(v,str) and (len(v)>32 or '"' in v)) for v in args.values()):raise ValueError('invalid argument value')
    req={'cmd':'action','party':p,'action':data['action'],**args};req['cmd']='action';req['party']=p
    W.seq[p]+=1;W.last[p]=time.monotonic();result=W.call(req)
    self.reply({'result':result,'sequence':W.seq[p]})
  except PermissionError as e:self.reply({'error':str(e)},401)
  except (ValueError,TypeError) as e:self.reply({'error':str(e)},400)
  except Exception as e:self.reply({'error':str(e)},500)
if __name__=='__main__':
 (ROOT/'evidence').mkdir(exist_ok=True)
 server=ThreadingHTTPServer(('127.0.0.1',PORT),Handler)
 PORT=server.server_port
 print('Original-engine Level 4: http://127.0.0.1:'+str(PORT),flush=True)
 try:server.serve_forever()
 finally:W.running=False;W.child.terminate();W.child.wait(timeout=3)
