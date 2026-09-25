#!/usr/bin/env python3
"""Run alternating paired Plan 34 trials from a frozen manifest. No adoption without quality review."""
import argparse
import json
import pathlib
import statistics
import subprocess
import sys
from adjudication_quality import check_outcomes, inference_totals, check_attribution, hard_issues


def summarize(rows):
    groups = {}
    for row in rows:
        groups.setdefault((row['scenario'], row['arm']), []).append(row)
    results = {}
    for (scenario, arm), trials in groups.items():
        times = [m['seconds_to_commit'] for t in trials for m in t['months'] if m['job']['status'] == 'complete']
        results[scenario + '/' + arm] = dict(
            successful_months=len(times), failed_runs=sum(t['returncode'] != 0 for t in trials),
            median=statistics.median(times) if times else None,
            range=[min(times), max(times)] if times else None,
            tail_evidence='insufficient', quality_passed=all(t.get('quality', {}).get('passed', False) for t in trials),
            audit=inference_totals([m for t in trials for m in t['months']]))
    return results


def adoption_gate(rows, candidate, scenarios, repeats):
    summary = summarize(rows)
    reasons, notes = [], []
    if repeats < 3:
        reasons.append('fewer than three paired repeats')
    baseline_times, candidate_times = [], []
    for name in scenarios:
        arms = [[r for r in rows if r['scenario'] == name and r['arm'] == arm] for arm in ('baseline', candidate)]
        if any(len(runs) < repeats or any(len(r['months']) != 2 for r in runs) for runs in arms):
            reasons.append(name + ': incomplete paired sample')
            continue
        if any(r['returncode'] or hard_issues(r.get('quality', {})) for r in arms[1]):
            reasons.append(name + ': failed candidate run or hard quality violation')
        # No loss of required outcomes relative to the paired baseline, which can miss them too.
        passed = [sum(bool(r.get('quality', {}).get('passed')) for r in runs) for runs in arms]
        if passed[1] < passed[0]:
            reasons.append(name + ': candidate lost required outcomes the baseline achieved')
        if passed[0] < len(arms[0]):
            notes.append(name + ': baseline missed required outcomes in %d/%d runs' % (len(arms[0])-passed[0], len(arms[0])))
        b, c = (summary[name+'/'+arm] for arm in ('baseline', candidate))
        if b['median'] is None or c['median'] is None:
            reasons.append(name + ': no successful timing')
            continue
        if c['median'] > 1.1*b['median']:
            reasons.append(name + ': median regression exceeds 10%')
        for metric in ('retries', 'truncations', 'failed_attempts'):
            if c['audit'][metric] > b['audit'][metric]:
                reasons.append(name + ': increased ' + metric)
        baseline_times.extend(m['seconds_to_commit'] for r in arms[0] for m in r['months'] if m['job']['status'] == 'complete')
        candidate_times.extend(m['seconds_to_commit'] for r in arms[1] for m in r['months'] if m['job']['status'] == 'complete')
    gain = 1-statistics.median(candidate_times)/statistics.median(baseline_times) if baseline_times and candidate_times else None
    if gain is None or gain < .15:
        reasons.append('overall median improvement below 15% or unavailable')
    return dict(mac_passed=not reasons, median_improvement=gain, reasons=reasons, baseline_notes=notes,
                windows='deferred by user; unverified', enable_default=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('--baseline-only', action='store_true', help='Measure all baseline scenarios without a candidate')
    parser.add_argument('--experiment', nargs='+', default=['compact'], choices=['compact', 'schema', 'reasoning', 'grounding', 'operations'],
                        help='One or more candidates; each scenario runs one shared baseline')
    parser.add_argument('--repeats', type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.repeats <= 10:
        parser.error('repeats must be 1..10; use at least three for finalists')
    manifest = json.loads(args.manifest.read_text())
    # Record these before execution, not inferred after seeing results.
    for key in ('model_sha256', 'quantization', 'llama_build', 'hardware', 'context', 'slots', 'compiler', 'sampling', 'scenarios'):
        if not manifest.get(key):
            parser.error('manifest requires ' + key)
    for scenario in manifest['scenarios']:
        for key in ('name', 'starting_save', 'schedule', 'reference_cache', 'required_outcomes', 'permitted_alternatives'):
            if key not in scenario:
                parser.error('scenario requires ' + key)
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2))
    rows = []
    active, stopped = list(dict.fromkeys(args.experiment)), {}

    def write_summary():
        names = [s['name'] for s in manifest['scenarios']]
        (args.output/'summary.json').write_text(json.dumps(dict(
            runs=[{k: v for k, v in r.items() if k != 'months'} for r in rows], summary=summarize(rows),
            stopped=stopped, adoption={e: adoption_gate(rows, e, names, args.repeats) for e in args.experiment}), indent=2))

    for repeat in range(args.repeats):
        for index, scenario in enumerate(manifest['scenarios']):
            if not args.baseline_only and not active:
                break
            arms = ['baseline'] + ([] if args.baseline_only else list(active))
            # Rotate so neither the baseline nor any candidate always runs first.
            shift = repeat % len(arms)
            for arm in arms[shift:] + arms[:shift]:
                if arm != 'baseline' and arm not in active:
                    continue
                report = args.output/f'{index}-{repeat}-{arm}.json'
                command = [sys.executable, str(pathlib.Path(__file__).with_name('living_world_probe.py')), str(report),
                           '--months', '2', '--budget', str(manifest.get('budget', 210))]
                for key in ('starting_save', 'schedule', 'reference_cache'):
                    path = (args.manifest.parent/scenario[key]).resolve()
                    command += ['--'+key.replace('_', '-'), str(path)]
                for key in ('binary', 'replay_binary'):
                    if key in manifest:
                        command += ['--'+key.replace('_', '-'), str((args.manifest.parent/manifest[key]).resolve())]
                if arm != 'baseline':
                    scenario_path = report.with_suffix('.scenario.json')
                    scenario_path.write_text(json.dumps(scenario, indent=2))
                    command += ['--experiment', arm, '--quality-scenario', str(scenario_path)]
                result = subprocess.run(command)
                months = json.loads(report.read_text()) if report.exists() else []
                replay_path = report.with_suffix('.replay.json')
                quality = check_outcomes(scenario, json.loads(replay_path.read_text()), len(months)) if replay_path.exists() and months and all(m['job']['status'] == 'complete' for m in months) else dict(passed=False, issues=['Run or replay failed'])
                quality.setdefault('issues', []).extend(check_attribution(months))
                quality['passed'] = quality.get('passed', False) and not quality['issues']
                rows.append(dict(scenario=scenario['name'], repeat=repeat, arm=arm, returncode=result.returncode, report=report.name, months=months, quality=quality))
                # A failed run or hard violation is evidence, never a quick successful sample.
                if arm != 'baseline' and (result.returncode or hard_issues(quality)):
                    active.remove(arm)
                    stopped[arm] = scenario['name'] + ': failed run or hard quality violation'
                write_summary()
            # Non-inferiority: stop a candidate that lost an outcome its paired baseline achieved.
            pair = {r['arm']: r for r in rows if r['scenario'] == scenario['name'] and r['repeat'] == repeat}
            for arm in list(active):
                if arm in pair and pair['baseline']['quality']['passed'] and not pair[arm]['quality']['passed']:
                    active.remove(arm)
                    stopped[arm] = scenario['name'] + ': lost an outcome the baseline achieved'
            write_summary()
    return 0 if args.baseline_only or active else 1


if __name__ == '__main__':
    sys.exit(main())
