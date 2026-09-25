#!/usr/bin/env python3
"""Bounded real-model campaign check. Writes an evidence report; never opens the user's save."""
import argparse, hashlib, json, pathlib, socket, sqlite3, subprocess, tempfile, time, urllib.request, shutil, platform
from benchmark_fixture import backup
from adjudication_quality import check_outcomes, check_attribution, hard_issues
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('output', type=pathlib.Path)
p.add_argument('--months', type=int, default=3)
p.add_argument('--country', default='PT')
p.add_argument('--first-order', default='', help='One order in month one; later months stay passive')
p.add_argument('--binary', type=pathlib.Path, default=ROOT/'build/localhistoria')
p.add_argument('--replay-binary', type=pathlib.Path, default=ROOT/'build/replay_verify')
p.add_argument('--starting-save', type=pathlib.Path, help='Closed disposable fixture database; never a save in saves/')
p.add_argument('--schedule', type=pathlib.Path, help='JSON array of monthly {orders, responses} objects')
p.add_argument('--reference-cache', type=pathlib.Path, help='Frozen JSON map from actor ID to cached reference')
p.add_argument('--prepare-only', type=pathlib.Path, help='Create a disposable starting fixture here, without inference')
p.add_argument('--quality-scenario', type=pathlib.Path, help='Predeclared scenario gate; stop candidate after a hard violation (missing outcomes are judged against the baseline)')
p.add_argument('--experiment', choices=['', 'reasoning', 'schema', 'compact', 'grounding', 'operations', 'offers'], default='')
p.add_argument('--reserve', type=float, default=0)
p.add_argument('--budget', type=float, default=210)
p.add_argument('--legacy-timing', action='store_true', help='Baseline lacks finished marker; wait for successful narration audit or record unavailable')
a = p.parse_args()
# Refuse campaign-directory input and output, symlinks included. SQLite backup reads only an
# explicitly supplied disposable fixture, never a default campaign path.
def safe_fixture(path):
    path = path.resolve()
    if path.is_relative_to((ROOT/'saves').resolve()): p.error('Benchmark paths must not use saves/')
    return path
for field in ('starting_save', 'prepare_only', 'output'):
    value = getattr(a, field)
    if value: setattr(a, field, safe_fixture(value))
schedule = json.loads(a.schedule.read_text()) if a.schedule else []
if not isinstance(schedule, list): p.error('schedule must be an array')
if a.schedule and len(schedule) != a.months: p.error('schedule must have exactly --months entries')

if not 0 < a.budget <= 1800: p.error('budget must be positive and at most 1800 seconds')
if not 1 <= a.months <= 24: p.error('months must be 1..24')
with socket.socket() as sock:
    sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
with tempfile.TemporaryDirectory(prefix='localhistoria-live-') as temp:
    folder = pathlib.Path(temp)
    cfg = folder / 'config.json'
    fixture_counts = backup(a.starting_save, folder/'save.sqlite') if a.starting_save else None
    snapshot_hash = hashlib.sha256((folder/'save.sqlite').read_bytes()).hexdigest() if a.starting_save else None
    cfg.write_text(json.dumps(dict(port=port, save_path=str(folder/'save.sqlite'), timeout_seconds=240, turn_budget_seconds=a.budget, mandatory_reserve_seconds=a.reserve, inference_experiment=a.experiment, reference_network=False)))
    def api(path, body=None):
        req = urllib.request.Request(f'http://127.0.0.1:{port}/api/v1/{path}', None if body is None else json.dumps(body).encode(), {'Content-Type':'application/json'})
        with urllib.request.urlopen(req, timeout=10) as response: return json.load(response)
    with (folder/'server.log').open('w') as log:
        server = subprocess.Popen([str(a.binary.resolve()), str(cfg)], cwd=ROOT, stdout=log, stderr=log)
        report = []
        try:
            startup_deadline = time.monotonic() + 60
            while time.monotonic() < startup_deadline:
                if server.poll() is not None: raise RuntimeError('Disposable server exited during startup')
                try: api('campaign'); break
                except OSError: time.sleep(.1)
            else: raise TimeoutError('Disposable server startup exceeded 60 seconds')
            state = api('campaign')['state'] if a.starting_save else api('campaign', {'country':a.country})['state']
            if a.reference_cache:
                with sqlite3.connect(folder/'save.sqlite') as db:
                    for actor, document in json.loads(a.reference_cache.read_text()).items():
                        db.execute('INSERT OR REPLACE INTO background VALUES(?,?)', (actor,json.dumps(document)))
            metadata = dict(platform=platform.platform(), hardware=platform.machine(), fixture_counts=fixture_counts,
                            experiment=a.experiment, reference_cache_sha256=hashlib.sha256(a.reference_cache.read_bytes()).hexdigest() if a.reference_cache else None,
                            binary=str(a.binary.resolve()), binary_sha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),
                            starting_snapshot_sha256=snapshot_hash,
                            schedule=schedule, config=json.loads(cfg.read_text()), token_unavailable=None)
            a.output.parent.mkdir(parents=True, exist_ok=True)
            cache = a.binary.resolve().parent/'CMakeCache.txt'
            metadata['compiler_cache'] = cache.read_text() if cache.exists() else None
            a.output.with_suffix('.config.json').write_text(json.dumps(metadata, indent=2))
            initial_replay = a.output.with_suffix('.initial.replay.json')
            initial_replay.write_text(json.dumps(api('replay')))
            subprocess.run([str(a.replay_binary.resolve()), str(initial_replay)], check=True, timeout=60)
            if a.prepare_only:
                a.prepare_only.parent.mkdir(parents=True, exist_ok=True)
                backup(folder/'save.sqlite', a.prepare_only)
                print('Prepared disposable fixture: '+str(a.prepare_only), flush=True)
            for_months = 0 if a.prepare_only else a.months
            for month in range(for_months):
                previous = state
                job = None
                started = time.monotonic()
                monthly = schedule[month] if schedule else {}
                job = api('turns', dict(campaign_id=state['campaign_id'], expected_turn=state['turn'], idempotency_key=f'live-month-{month}', orders=monthly.get('orders', [{'text':a.first_order}] if month == 0 and a.first_order else []), responses=monthly.get('responses', [])))
                while time.monotonic()-started < a.budget+5:
                    result = api('jobs?id='+str(job['id']))
                    if result['status'] != 'running': break
                    time.sleep(.25)
                else: raise TimeoutError('Month exceeded configured budget plus five seconds')
                row = dict(month=month+1, seconds_to_commit=round(time.monotonic()-started,2), job=result)
                # Commit and prose completion are distinct. Never start the next measured month
                # while narration from this one still owns the single inference slot.
                finished = None
                if result['status'] == 'complete':
                    while time.monotonic()-started < a.budget+5:
                        records = api('inference?job='+str(job['id']))
                        marker = next((r for r in records if r.get('kind') == 'turn_timing'), None)
                        narration = next((r for r in records if r.get('kind') == 'narrate'), None)
                        if marker or (a.legacy_timing and narration):
                            finished = time.monotonic()-started
                            break
                        time.sleep(.25)
                row['seconds_to_finished_prose'] = finished
                row['finished_timing_provenance'] = 'poll' if finished is not None else 'unavailable'
                campaign=api('campaign'); state=campaign['state']
                if result['status']=='complete':
                    turn=api('replay')['turns'][-1]
                    row.update(allocations=turn['reference']['allocations'], intentions=turn['reference']['contexts']['intentions'], events=turn['reference']['delta']['events'], changes=turn['reference']['delta']['changes'])
                records = api('inference?job='+str(job['id']))
                row['inference'] = records
                row['timing'] = next((r for r in records if r.get('kind') == 'turn_timing'), None)
                row['controllers_changed'] = [dict(region=r, owner=t['owner'], before=previous['territories'][r]['controller'], after=t['controller']) for r,t in state['territories'].items() if t['controller'] != previous['territories'][r]['controller']]
                row['conflicts'] = state['conflicts']
                row['proposals'] = state['proposals']
                if result['status']=='complete':
                    row['authoritative_events'] = turn['events']
                    advances = [e for e in turn['events'] if e['action']=='front_advance']
                    if advances:
                        mapped = api('events?on_map=1&limit=500')['events']
                        for event in advances:
                            assert any(c['id']==event['region'] for c in campaign['journal'][0]['changes']), event
                            assert any(e['turn']==state['turn'] and e['regions']==[event['region']] and event['target'] in e['conflicts'] for e in mapped), event
                    row['front_presentation_verified'] = len(advances)
                # Tokenize retained reasoning after finished prose, outside the measured turn.
                # This is measurement only; it never adds a production inference request.
                for record in records:
                    record['reasoning_tokens'] = None
                    record['reasoning_token_provenance'] = 'unavailable'
                    response = record.get('response') or {}
                    choices = response.get('choices') or []
                    if choices:
                        message = choices[0].get('message', {})
                        reasoning = message.get('reasoning_content') or message.get('reasoning') or ''
                        if not reasoning:
                            record['reasoning_tokens'] = 0
                            record['reasoning_token_provenance'] = 'empty response reasoning'
                        else:
                            try:
                                req = urllib.request.Request('http://127.0.0.1:8081/tokenize',
                                    json.dumps(dict(content=reasoning, add_special=False)).encode(), {'Content-Type':'application/json'})
                                with urllib.request.urlopen(req, timeout=10) as counted:
                                    record['reasoning_tokens'] = len(json.load(counted)['tokens'])
                                record['reasoning_token_provenance'] = 'post-turn tokenizer, excludes reasoning delimiters'
                            except Exception: pass
                for summary,record in zip(row['inference'],records):
                    summary['schema_bytes'] = record.get('schema_bytes') if 'schema_bytes' in record else (len(json.dumps(record['schema'],separators=(',',':'),ensure_ascii=False).encode()) if 'schema' in record else None)
                report.append(row)
                if a.quality_scenario and result['status'] == 'complete':
                    scenario = json.loads(a.quality_scenario.read_text())
                    # Access/ceasefire needs both scheduled offers before judging coverage.
                    if scenario['name'] != 'access_ceasefire' or month+1 == for_months:
                        row['quality'] = check_outcomes(scenario, api('replay'), len(report))
                        row['quality']['issues'].extend(check_attribution(report))
                        row['quality']['passed'] = not row['quality']['issues']
                a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2))
                print(json.dumps({k:row[k] for k in ('month','seconds_to_commit','job')}),flush=True)
                if result['status']!='complete' or hard_issues(row.get('quality', {})): break
            replay=a.output.with_suffix('.replay.json');replay.write_text(json.dumps(api('replay')))
            subprocess.run([str(a.replay_binary.resolve()),str(replay)],check=True, timeout=60)
            if report and hard_issues(report[-1].get('quality', {})):
                raise RuntimeError('Candidate rejected by predeclared quality gate: '+str(report[-1]['quality']))
            if report and report[-1]['job']['status'] != 'complete':
                raise RuntimeError(report[-1]['job'])
        except Exception as error:
            a.output.parent.mkdir(parents=True, exist_ok=True)
            if 'started' in locals() and (not report or report[-1]['month'] != month+1):
                failed = dict(month=month+1, elapsed_seconds=time.monotonic()-started, job=dict(status='probe_failed', error=str(error)))
                if 'job' in locals() and job:
                    try: failed['inference'] = api('inference?job='+str(job['id']))
                    except Exception: pass
                report.append(failed)
            a.output.write_text(json.dumps(report, indent=2))
            a.output.with_suffix('.failure.json').write_text(json.dumps(dict(error=str(error), elapsed_seconds=time.monotonic()-started if 'started' in locals() else None), indent=2))
            raise
        finally:
            server.terminate()
            try:server.wait(5)
            except subprocess.TimeoutExpired:server.kill();server.wait()
            log.flush();a.output.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(folder/'server.log',a.output.with_suffix('.server.log'))
