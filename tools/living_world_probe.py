#!/usr/bin/env python3
"""Bounded real-model campaign check. Writes an evidence report; never opens the user's save."""
import argparse, json, pathlib, socket, subprocess, tempfile, time, urllib.request
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('output', type=pathlib.Path)
p.add_argument('--months', type=int, default=3)
p.add_argument('--country', default='PT')
a = p.parse_args()
if not 1 <= a.months <= 6: p.error('months must be 1..6')
with socket.socket() as sock:
    sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
with tempfile.TemporaryDirectory(prefix='localhistoria-live-') as temp:
    folder = pathlib.Path(temp)
    cfg = folder / 'config.json'
    cfg.write_text(json.dumps(dict(port=port, save_path=str(folder/'save.sqlite'), timeout_seconds=240)))
    def api(path, body=None):
        req = urllib.request.Request(f'http://127.0.0.1:{port}/api/v1/{path}', None if body is None else json.dumps(body).encode(), {'Content-Type':'application/json'})
        with urllib.request.urlopen(req, timeout=10) as response: return json.load(response)
    with (folder/'server.log').open('w') as log:
        server = subprocess.Popen([str(ROOT/'build/localhistoria'), str(cfg)], cwd=ROOT, stdout=log, stderr=log)
        report = []
        try:
            for _ in range(100):
                try: api('campaign'); break
                except OSError: time.sleep(.1)
            state = api('campaign', {'country':a.country})['state']
            for month in range(a.months):
                started = time.monotonic()
                job = api('turns', dict(campaign_id=state['campaign_id'], expected_turn=state['turn'], idempotency_key=f'live-month-{month}', orders=[]))
                for _ in range(600):
                    result = api('jobs?id='+str(job['id']))
                    if result['status'] != 'running': break
                    time.sleep(1)
                else: raise RuntimeError('Bounded month exceeded ten minutes')
                row = dict(month=month+1, seconds_to_commit=round(time.monotonic()-started,2), job=result)
                campaign=api('campaign'); state=campaign['state']
                if result['status']=='complete':
                    turn=api('replay')['turns'][-1]
                    row.update(allocations=turn['reference']['allocations'], intentions=turn['reference']['contexts']['intentions'], events=turn['reference']['delta']['events'], changes=turn['reference']['delta']['changes'])
                row['inference']=[{**{k:r.get(k) for k in ('kind','latency_ms','prompt_tokens','model','thinking')},'cached_tokens':(r.get('response') or {}).get('usage',{}).get('prompt_tokens_details',{}).get('cached_tokens')} for r in api('inference?job='+str(job['id']))]
                report.append(row);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2))
                print(json.dumps({k:row[k] for k in ('month','seconds_to_commit','job')}),flush=True)
                if result['status']!='complete': break
            replay=folder/'replay.json';replay.write_text(json.dumps(api('replay')))
            subprocess.run([str(ROOT/'build/replay_verify'),str(replay)],check=True)
        finally:
            server.terminate()
            try:server.wait(5)
            except subprocess.TimeoutExpired:server.kill();server.wait()
