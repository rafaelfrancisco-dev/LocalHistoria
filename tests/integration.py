#!/usr/bin/env python3
"""Real HTTP/SQLite integration; all campaigns use disposable temporary saves."""
import concurrent.futures, http.server, json, os, pathlib, socket, sqlite3, subprocess, tempfile, threading, time, urllib.request, urllib.error
ROOT=pathlib.Path(__file__).resolve().parents[1]
def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
class Mock(http.server.BaseHTTPRequestHandler):
 mode='valid'; requests=[]
 def log_message(self,*a):pass
 def respond(self,result):
  self.send_response(200);self.end_headers();self.wfile.write(json.dumps(result).encode())
 def do_GET(self):self.respond({'default_generation_settings':{'n_ctx':8192}})
 def do_POST(self):
  body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
  if self.path=='/apply-template':self.respond({'prompt':json.dumps(body['messages'])});return
  if self.path=='/tokenize':self.respond({'tokens':[0]*1000});return
  Mock.requests.append(body)
  if Mock.mode=='slow':time.sleep(1.5)
  raw=body['messages'][1]['content']
  if Mock.mode=='malformed':
   result={'orders':[{'action':'invade'}]}
  elif 'intentions' in json.loads(raw):
   result={'events':[]}
   value=json.loads(raw);intentions=[i['action'] for i in value['intentions'] if i['actor']=='PT'];fixture=next((o for o in intentions if 'transfer-and-build-fixture' in o),None)
   if fixture:
    rid=fixture.split('(region ')[1].split(')')[0]
    result={'events':[{'actor':'PT','blocked_by':'none','description':'Fixture','causes':[],'operations':[{'action':'build','kind':'port','name':'Fixture port','actor':'PT','region':rid},{'action':'transfer_territory','region':rid,'to':'ES'}]}]}
   elif intentions:
    result={'events':[{'actor':'PT','blocked_by':'none','description':text,'causes':[],'operations':[]} for text in intentions]}
   existing=result['events'];result['events']=[next((e for e in existing if e['actor']==i['actor'] and (e['description']==i['action'] or fixture)),{'actor':i['actor'],'blocked_by':'none','description':i['action'],'causes':[],'operations':[]}) for i in value['intentions']]
   proposing=any('proposal-fixture' in o for o in intentions)
   for i,e in zip(value['intentions'],result['events']):
    if proposing and i['actor']!='PT' and not i.get('regional_actor'):
     e['operations']=[{'action':'add_commitment','actors':[i['actor'],'PT'],'kind':'trade_agreement','months':6,'name':'Fixture Accord','terms':[],'text':'Tariffs cut'}];proposing=False
    if 'railway' in i['action']:e['effects']=[{'actor':'PT','economy':'strong_growth','stability':'none'}]
    if i.get('regional_actor'):
     e['operations']=[{'action':'regional_initiative','region':i['regional_actor'],'agenda':'Local civic representation','relations':'cooperative','demand':'Request a regional assembly'}]
    if 'war-fixture' in i['action']:
     e['operations']=[{'action':'open_conflict','actors':['PT','ES'],'kind':'war','intensity':'medium','name':'War','text':'Border war with Spain'}]
     e['effects']=[{'actor':'PT','economy':'decline','stability':'decline'}]
    if 'unreachable-fixture' in i['action']:
     e['operations']=[{'action':'occupy','region':'nowhere-fixture','by':'PT'}]
    if 'identity-fixture' in i['action']:
     e['operations']=[{'action':'change_identity','actor':i['actor'],'name':'Atlantic Republic','change_type':'rename' if 'refound' not in i['action'] else 'refounding','reason':'Explicit constitutional settlement','flag_design':{'layout':'horizontal','palette':['#123456','#abcdef','#987654'],'emblem':'circle','explanation':'Shared civic future'}}]
  elif 'authoritative_events' in raw:
   result={'text':'A quiet month of fictional administration.'}
  elif 'previous_chronicle' in raw or 'campaign_events' in raw:
   result={'summary':'Fixture chronicle'}
  else:
   value=json.loads(raw);facts=value.get('facts',{});result={'actions':[{'text':'Discuss a local civic initiative' if facts.get('initiative_type')=='independent_regional' else 'Discuss a national civic initiative'}] if facts.get('initiative_type','').startswith('independent_') else []}
  content=json.dumps(result)
  response=json.dumps({'choices':[{'message':{'content':content}}]}).encode()
  try:self.send_response(200);self.end_headers();self.wfile.write(response)
  except (BrokenPipeError,ConnectionResetError):pass
class Server:
 def __init__(self,folder,endpoint=None,timeout=.25,extra={}):
  self.p=port();self.base=f'http://127.0.0.1:{self.p}';self.config=folder/'config.json';self.save=folder/'save.sqlite';self.log=open(folder/'server.log','a')
  self.config.write_text(json.dumps({'port':self.p,'save_path':str(self.save),'timeout_seconds':timeout,'reference_network':False,**extra}))
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
   j=d.get('turns',order(wd,'down-turn',[{'text':'Offer an alliance to Spain'}]),202)
   jr=d.poll(j['id']);assert jr['status']=='failed' and jr['error']
   assert d.get('campaign')['state']==wd
  finally:d.stop()

  budgetfolder=folder/'budget';budgetfolder.mkdir();b=Server(budgetfolder,f'http://127.0.0.1:{mock.server_port}',5,{'turn_budget_seconds':3});b.start()
  try:
   wb=b.get('campaign',{'country':'PT'})['state']
   Mock.mode='slow';j=b.get('turns',order(wb,'cancel-turn',[{'text':'Send envoys to France'}]),202)
   b.get('turns/cancel',{'job':str(j['id'])},202)
   jr=b.poll(j['id']);assert jr['status']=='failed' and jr['error']=='Cancelled by the player',jr
   assert b.get('campaign')['state']==wb
   b.get('turns/cancel',{'job':str(j['id'])},409);b.get('turns/cancel',{'job':str(j['id']),'force':True},409)
   j=b.get('turns',order(wb,'budget-turn',[{'text':'Send envoys to France'}]),202)
   jr=b.poll(j['id']);assert jr['status']=='failed' and jr['error']=='Month exceeded its time budget',jr
   assert b.get('campaign')['state']==wb
   Mock.mode='valid'
   print('Turn cancel and month budget verified',flush=True)
  finally:b.stop()

  s=Server(folder,f'http://127.0.0.1:{mock.server_port}');s.start()
  try:
   created=s.get('campaign',{'country':'PT'});w=created['state'];assert w['turn']==0 and len(w['actors'])==242 and 'countries' not in w
   pt=created['national_status']['PT'];assert 10_000_000<pt['population']<11_500_000 and pt['gdp'].startswith('€') and 'US$' in pt['gdp'] and pt['stability'],pt
   assert 23800 <= pt['military']['active'] <= 32200 and pt['military']['uncommitted_personnel'] == pt['military']['active'],pt
   names={c['name'] for c in w['conflicts'].values()}|{c['name'] for c in w['commitments'].values()};assert {'Russo-Ukrainian War','North Atlantic Treaty Organization','European Union'}<=names,names
   assert any(t['controller']=='RU' and t['owner']=='UA' for t in w['territories'].values())
   assert s.get('country?code=PT')['status']['population']==pt['population']
   s.get('turns',order(w,'illegal-target',[{'text':'Invade','targets':['ZZ']}]),409)
   s.get('turns',order(w,'typed-action',[{'action':'economy'}]),409)
   s.get('turns',dict(order(w,'bad-narration',[]),narration='no'),409)
   rows=s.get('countries');assert any(r['code']=='ES' and r.get('neighbor') for r in rows) and not any(r['code']=='FR' and r.get('neighbor') for r in rows) and next(r for r in rows if r['code']=='PT')['has_flag'],rows[:3]
   assert s.get('campaign')['state']==w
   Mock.mode='slow';payload=order(w,'first-turn',[{'text':'Propose an alliance to Spain','targets':['ES']},{'text':'Invest in the national railway network'}])
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
   current=s.get('campaign')['state'];assert current['turn']==1
   journal=s.get('campaign')['journal'][0]['events'];events=[e['text'] for e in journal if e['action']!='war_toll'];assert len(events)==5 and all(e.startswith(o['text']) for e,o in zip(events[:2],payload['orders'])),events
   tolls=[e['text'] for e in journal if e['action']=='war_toll'];assert any(t.startswith('Russo-Ukrainian War (high intensity): about ') for t in tolls),tolls
   assert current['conflicts']['c0-1']['casualties']>0 and current['territories'] != w['territories']
   s.get('turns',order(w,'stale-turn',[]),409)
   j=s.get('turns',order(current,'second-turn',[{'text':'war-fixture: declare war on Spain','targets':['ES']}]),202);assert s.poll(j['id'])['status']=='complete'
   current=s.get('campaign')['state'];assert current['turn']==2
   war=next(c for c in current['conflicts'].values() if c['actors']==['PT','ES']);assert war['name']=='Portugal–Spain War' and war['kind']=='war' and war['started_turn']==2,war
   assert war['intensity']=='medium' and war['affected_regions'] and war['casualties']>0 and war['displaced']>0,war
   assert current['actors']['PT']['status']['stability']<w['actors']['PT']['status']['stability']
   changes=[c['summary'] for c in s.get('campaign')['journal'][0]['changes']];assert 'Portugal–Spain War opened' in changes,changes
   with sqlite3.connect(s.save) as db:db.execute('INSERT OR REPLACE INTO background VALUES(?,?)',('PT',json.dumps({'available':True,'revision':77,'excerpt':'fixture'})))
   j=s.get('turns',dict(order(current,'third-turn',[{'text':'proposal-fixture: hold talks'}]),narration=False),202);assert s.poll(j['id'])['status']=='complete'
   month=s.get('campaign')['journal'][0];assert month['narration_fallback'] in (1,'1',True) and 'fictional administration' not in month['narration'],month['narration']
   current=s.get('campaign')['state'];offers=[k for k,p in current['proposals'].items() if p['status']=='pending'];assert len(offers)==1 and offers[0] not in current['commitments'],current['proposals']
   s.get('turns',dict(order(current,'bad-response',[]),responses=[{'proposal':'k0-9','accept':True}]),409)
   j=s.get('turns',dict(order(current,'fourth-turn',[]),responses=[{'proposal':offers[0],'accept':True}]),202);assert s.poll(j['id'])['status']=='complete'
   current=s.get('campaign')['state'];assert current['proposals'][offers[0]]['status']=='accepted' and current['commitments'][offers[0]]['name']=='Fixture Accord' and current['commitments'][offers[0]]['expires_turn']==current['turn']+6,current['commitments']
   with sqlite3.connect(s.save) as db:db.execute('UPDATE background SET document=? WHERE code=?',(json.dumps({'available':True,'revision':88,'excerpt':'new fixture'}),'PT'))
   export=s.get('replay');assert export['turns'][-1]['reference']['PT']['revision']==77
   replayfile=folder/'replay.json';replayfile.write_text(json.dumps(export));subprocess.run([str(ROOT/'build/replay_verify'),str(replayfile)],check=True)
   legacy_initial=json.loads(json.dumps(export['initial_state']))
   for key in ('regional_politics','activity'):legacy_initial.pop(key,None)
   legacy_state=json.loads(json.dumps(legacy_initial));legacy_state['turn']=1
   legacy={'initial_state':legacy_initial,'campaign':legacy_state,'geography':{},'turns':[{'state':legacy_state,'events':[],'intentions':{},'reference':{'delta':{'changes':[],'events':[]}}}]}
   legacyfile=folder/'legacy-replay.json';legacyfile.write_text(json.dumps(legacy));subprocess.run([str(ROOT/'build/replay_verify'),str(legacyfile)],check=True)
   with sqlite3.connect(s.save) as db:
    allocations=json.loads(db.execute('SELECT slots FROM monthly_allocations WHERE month=1').fetchone()[0])
   assert allocations==export['turns'][0]['reference']['allocations']
   for turn in export['turns']:
    independent=[e for e in turn['reference']['delta']['events'] if e.get('initiative_type','').startswith('independent_')]
    assert len(independent)==3 and sum('regional_actor' in e for e in independent)==1
   for body in Mock.requests:
    inp=json.loads(body['messages'][1]['content']);facts=inp.get('facts',{})
    if facts.get('initiative_type','').startswith('independent_'):assert 'player_actions' not in facts and 'intentions' not in inp
   # Chronicles only for the player and nations at war or in a small pact with it, never one-off independent actors.
   bound={'PT'}|{a for c in [*current['conflicts'].values(),*current['commitments'].values()] if 'PT' in c['actors'] and len(c['actors'])<=3 for a in c['actors']}
   names={current['actors'][a]['name'] for a in bound}
   chronicled=[json.loads(b['messages'][1]['content'])['facts']['actor_name'] for b in Mock.requests if 'campaign_events' in json.loads(b['messages'][1]['content'])]
   independent={current['actors'][slot['actor']]['name'] for turn in export['turns'] for slot in turn['reference']['allocations'] if slot['region']=='' and slot['actor'] not in bound}
   assert chronicled and set(chronicled)<=names and not set(chronicled)&independent,(chronicled,independent)
   export['turns'][0]['state']['turn']+=1;replayfile.write_text(json.dumps(export));assert subprocess.run([str(ROOT/'build/replay_verify'),str(replayfile)],capture_output=True).returncode==1
   current=s.get('campaign')['state'];s.stop();s.start();assert s.get('campaign')['state']==current
   Mock.mode='slow';j=s.get('turns',order(current,'interrupt-turn',[{'text':'Send envoys to France'}]),202)
   s.proc.kill();s.proc.wait();s.start();assert s.get('jobs?id='+str(j['id']))['status']=='interrupted';assert s.get('campaign')['state']==current
   assert Mock.requests and all((b['max_tokens']==768 or 1536<=b['max_tokens']<=4096+768) and b['response_format']['type']=='json_schema' for b in Mock.requests)
   print('HTTP integration passed: duplicates, overlap, stale requests, down/timeout/malformed model hard failures, free-text actions, treaty proposals, national status, named wars and war tolls, frozen references, restart and interrupted jobs',flush=True)
  finally:s.stop()

  # A save from before national status gains it on its next month and still replays exactly.
  legacyfolder=folder/'legacy';legacyfolder.mkdir();l=Server(legacyfolder,f'http://127.0.0.1:{mock.server_port}');l.start()
  try:
   Mock.mode='valid';old=l.get('campaign',{'country':'PT'})['state']
   for a in old['actors'].values():a.pop('status',None)
   for t in old['territories'].values():
    for k in ('population','gdp_usd','displaced','occupation_gdp'):t.pop(k,None)
    t['controller']=t['owner']
   old['actors']={k:v for k,v in old['actors'].items() if len(k)==2};old['conflicts']={};old['commitments']={}
   l.stop()
   with sqlite3.connect(l.save) as db:db.execute('UPDATE campaign SET state=?',(json.dumps(old),));db.execute('UPDATE initial_world SET state=?',(json.dumps(old),))
   l.start();assert 'national_status' in l.get('campaign')
   j=l.get('turns',order(old,'legacy-turn',[]),202);assert l.poll(j['id'])['status']=='complete'
   upgraded=l.get('campaign');assert upgraded['state']['actors']['PT']['status'] and upgraded['national_status']['PT']['population']>10_000_000 and not upgraded['state']['conflicts']
   legacyexport=legacyfolder/'replay.json';legacyexport.write_text(json.dumps(l.get('replay')));subprocess.run([str(ROOT/'build/replay_verify'),str(legacyexport)],check=True)
   print('Legacy save gained national status and replayed',flush=True)
  finally:l.stop()

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
   browser_export=folder/'browser-replay.json';browser_export.write_text(json.dumps(b.get('replay')));subprocess.run([str(ROOT/'build/replay_verify'),str(browser_export)],check=True)
   saved=b.get('campaign')['state'];b.stop();b.start();assert b.get('campaign')['state']==saved
   print('Browser campaign also survived server restart')
  finally:b.stop()
 finally:mock.shutdown()
