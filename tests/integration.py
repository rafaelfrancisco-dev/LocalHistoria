#!/usr/bin/env python3
"""Real HTTP/SQLite integration; all campaigns use disposable temporary saves."""
import concurrent.futures, http.server, json, os, pathlib, socket, sqlite3, subprocess, tempfile, threading, time, urllib.request, urllib.error
ROOT=pathlib.Path(__file__).resolve().parents[1]
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
class Mock(http.server.BaseHTTPRequestHandler):
 mode='valid'; requests=[]
 def log_message(self,*a):pass
 def do_POST(self):
  body=json.loads(self.rfile.read(int(self.headers['Content-Length'])));Mock.requests.append(body)
  if Mock.mode=='slow':time.sleep(1.5)
  raw=body['messages'][1]['content']
  if Mock.mode=='malformed':
   result={'orders':[{'action':'invade'}]}
  elif 'authoritative_events' in raw:
   result={'text':'A quiet month of fictional administration.'}
  elif 'player_instruction' in raw:
   facts=json.loads(raw);text=facts['player_instruction'].lower()
   result={'orders':[{'action':a} for w,a in [('econom','economy'),('army','army'),('navy','navy')] if w in text]}
  else:
   facts=json.loads(raw);actor=facts['facts']['actor']
   incoming=[o for o in facts['facts'].get('offers',[]) if o.endswith(':'+actor)]
   result={'orders':[{'action':'accept','target':incoming[0].split(':')[0]}] if incoming else []}
  content=json.dumps(result)
  response=json.dumps({'choices':[{'message':{'content':content}}]}).encode()
  try:self.send_response(200);self.end_headers();self.wfile.write(response)
  except (BrokenPipeError,ConnectionResetError):pass
class Server:
 def __init__(self,folder,endpoint=None,timeout=.25):
  self.p=port();self.base=f'http://127.0.0.1:{self.p}';self.config=folder/'config.json';self.save=folder/'save.sqlite';self.log=open(folder/'server.log','a')
  self.config.write_text(json.dumps({'port':self.p,'save_path':str(self.save),'timeout_seconds':timeout}))
  self.endpoint=endpoint
 def start(self):
  args=[str(ROOT/'build/localhistoria'),str(self.config)]
  if self.endpoint:args.append(f'--llama-endpoint={self.endpoint}')
  self.proc=subprocess.Popen(args,cwd=ROOT,stdout=self.log,stderr=self.log)
  for _ in range(100):
   if self.proc.poll() is not None:raise RuntimeError('Server exited; '+str(self.config))
   try:self.get('campaign');return
   except (ConnectionError,OSError):time.sleep(.05)
  raise RuntimeError('Server did not start')
 def stop(self):
  self.proc.terminate()
  try:self.proc.wait(4)
  except subprocess.TimeoutExpired:self.proc.kill();self.proc.wait()
 def get(self,path,body=None,expected=200):
  data=None if body is None else json.dumps(body).encode();req=urllib.request.Request(self.base+'/api/v1/'+path,data,{'Content-Type':'application/json'})
  try:
   with urllib.request.urlopen(req,timeout=5) as r:status=r.status;result=json.load(r)
  except urllib.error.HTTPError as e:status=e.code;result=json.load(e)
  assert status==expected,(path,status,result);return result
 def poll(self,id):
  for _ in range(100):
   j=self.get('jobs?id='+str(id))
   if j['status']!='running':return j
   time.sleep(.05)
  raise AssertionError('Job timeout')
def order(state,key,orders):return dict(campaign_id=state['campaign_id'],expected_turn=state['turn'],idempotency_key=key,orders=orders)
with tempfile.TemporaryDirectory(prefix='localhistoria-tests-') as tmp:
 folder=pathlib.Path(tmp);mock=http.server.ThreadingHTTPServer(('127.0.0.1',0),Mock);threading.Thread(target=mock.serve_forever,daemon=True).start()
 try:
  downfolder=folder/'down';downfolder.mkdir();d=Server(downfolder,'http://127.0.0.1:1');d.start()
  try:
   wd=d.get('campaign',{'country':'PT'})['state']
   j=d.get('turns',order(wd,'down-turn',[{'action':'offer','target':'ES'}]),202)
   jr=d.poll(j['id']);assert jr['status']=='failed' and jr['error']
   assert d.get('campaign')['state']==wd
  finally:d.stop()

  s=Server(folder,f'http://127.0.0.1:{mock.server_port}');s.start()
  try:
   w=s.get('campaign',{'country':'PT'})['state'];assert w['turn']==0 and len(w['countries'])==239
   s.get('turns',order(w,'illegal-target',[{'action':'improve','target':'ZZ'}]),409)
   assert s.get('campaign')['state']==w
   Mock.mode='slow';payload=order(w,'first-turn',[{'action':'offer','target':'ES'},{'action':'improve','target':'ES'}])
   with concurrent.futures.ThreadPoolExecutor() as pool:results=list(pool.map(lambda _:s.get('turns',payload,202),range(4)))
   assert len({j['id'] for j in results})==1
   s.get('turns',order(w,'overlap-turn',[]),409)
   j=s.poll(results[0]['id']);assert j['status']=='failed' and j['error']
   assert s.get('campaign')['state']==w
   Mock.mode='malformed';j=s.get('turns',order(w,'malformed-turn',payload['orders']),202)
   assert s.poll(j['id'])['status']=='failed'
   assert s.get('campaign')['state']==w
   Mock.mode='valid';j=s.get('turns',order(w,'valid-first-turn',payload['orders']),202)
   assert s.poll(j['id'])['status']=='complete'
   current=s.get('campaign')['state'];assert current['turn']==1 and current['offers']==['PT:ES']
   s.get('turns',order(w,'stale-turn',[]),409)
   j=s.get('turns',order(current,'second-turn',[]),202);assert s.poll(j['id'])['status']=='complete'
   current=s.get('campaign')['state'];assert current['alliances']==['ES:PT'] and current['offers']==[]
   with sqlite3.connect(s.save) as db:db.execute('INSERT INTO background VALUES(?,?)',('PT',json.dumps({'available':True,'revision':77,'excerpt':'fixture'})))
   j=s.get('turns',order(current,'third-turn',[]),202);assert s.poll(j['id'])['status']=='complete'
   with sqlite3.connect(s.save) as db:db.execute('UPDATE background SET document=? WHERE code=?',(json.dumps({'available':True,'revision':88,'excerpt':'new fixture'}),'PT'))
   export=s.get('replay');assert export['turns'][-1]['reference']['PT']['revision']==77
   replayfile=folder/'replay.json';replayfile.write_text(json.dumps(export));subprocess.run([str(ROOT/'build/replay_verify'),str(replayfile)],check=True)
   export['turns'][0]['state']['countries']['PT']['treasury']+=1;replayfile.write_text(json.dumps(export));assert subprocess.run([str(ROOT/'build/replay_verify'),str(replayfile)],capture_output=True).returncode==1
   current=s.get('campaign')['state'];s.stop();s.start();assert s.get('campaign')['state']==current
   Mock.mode='slow';j=s.get('turns',order(current,'interrupt-turn',[{'action':'improve','target':'FR'}]),202)
   s.proc.kill();s.proc.wait();s.start();assert s.get('jobs?id='+str(j['id']))['status']=='interrupted';assert s.get('campaign')['state']==current
   assert Mock.requests and all(b['max_tokens']==768 and b['response_format']['type']=='json_schema' for b in Mock.requests)
   print('HTTP integration passed: duplicates, overlap, stale requests, down/timeout/malformed model hard failures, alliances, frozen references, restart and interrupted jobs',flush=True)
  finally:s.stop()

  cfgfolder=folder/'config-test';cfgfolder.mkdir()
  endpointA=f'http://127.0.0.1:{mock.server_port}';endpointB='http://127.0.0.1:1'
  c=Server(cfgfolder,endpointA);c.start()
  try:
   assert c.get('campaign')['llama_endpoint']==endpointA
   assert c.get('model-config',{'llama_endpoint':endpointB})['llama_endpoint']==endpointB
   assert c.get('campaign')['llama_endpoint']==endpointB
   c.get('model-config',{'llama_endpoint':'http://127.0.0.1:'},409)
   assert c.get('campaign')['llama_endpoint']==endpointB
   c.stop();c.endpoint=None;c.start()
   assert c.get('campaign')['llama_endpoint']==endpointB
   c.stop();c.endpoint=endpointA;c.start()
   assert c.get('campaign')['llama_endpoint']==endpointA
   c.stop();c.endpoint=None;c.start()
   assert c.get('campaign')['llama_endpoint']==endpointA
   print('CLI flag / web UI model endpoint persistence verified',flush=True)
  finally:c.stop()

  browserfolder=folder/'browser';browserfolder.mkdir();b=Server(browserfolder,f'http://127.0.0.1:{mock.server_port}');b.start()
  try:
   Mock.mode='valid'
   subprocess.run(['node','tests/browser.mjs'],cwd=ROOT,env=dict(os.environ,LOCALHISTORIA_TEST_URL=b.base),check=True)
   saved=b.get('campaign')['state'];b.stop();b.start();assert b.get('campaign')['state']==saved
   print('Browser campaign also survived server restart')
  finally:b.stop()
 finally:mock.shutdown()
