"""Predeclared Plan 34 outcome gates; reads authoritative replay state, never model prose."""
import re

MISSING_OUTCOME = 'Required scenario outcome missing'


def hard_issues(quality):
    """Violations that reject a candidate outright. A missing outcome is judged against the
    paired baseline instead: the 4B baseline itself misses some outcomes."""
    return [i for i in quality.get('issues', []) if i != MISSING_OUTCOME]


def check_outcomes(scenario, replay, measured_turns):
    name = scenario['name']
    turns = replay['turns'][-measured_turns:] if measured_turns else []
    issues = []
    outcomes = []
    initial = replay['initial_state']
    # Starting fixture may contain recorded setup/history turns.
    if len(replay['turns']) > measured_turns:
        initial = replay['turns'][-measured_turns-1]['state']
    player = initial['player']
    for turn in turns:
        state = turn['state']
        for pid, proposal in state.get('proposals', {}).items():
            if proposal.get('status') == 'accepted':
                revision = proposal.get('revision')
                answers = proposal.get('answers', {})
                if not all(answers.get(a, {}).get('accept') is True and answers[a].get('revision') == revision
                           for a in proposal.get('required_signatories', proposal['actors'])):
                    issues.append('Unauthorized signature: ' + pid)
        for rid, region in state.get('regional_politics', {}).items():
            for demand in region.get('demands', []):
                if demand.get('fulfillment', {}).get('state') == 'fulfilled' and demand.get('terms', {}).get('kind') == 'regional_project':
                    ruler = demand.get('responding_authority', demand.get('authority_at_creation', player))
                    projects = state['actors'].get(ruler, {}).get('status', {}).get('projects', {})
                    project = projects.get(demand.get('fulfillment', {}).get('project'), {})
                    if project.get('target') != rid or project.get('state') != 'completed' or project.get('benefit') != demand['terms'].get('benefit'):
                        issues.append('Demand fulfilled without completed project: ' + demand['id'])
        # Attribution is checked by the provider; explicit failed attempts are also gated by the runner.
        if name in ('peace', 'older'):
            projects = state['actors'][player]['status'].get('projects', {})
            ok = any(p.get('category') == 'reform' and p.get('target') == player and
                     p.get('benefit') == 'public_services' and p.get('paid', 0) > 0 for p in projects.values())
            outcomes.append(ok)
        elif name == 'invasion':
            wars = [c for c in state['conflicts'].values() if player in c.get('actors', []) and 'ES' in c.get('actors', [])]
            captured = [r for rid, r in state['territories'].items() if initial['territories'][rid]['owner'] == 'ES' and r['controller'] == player]
            outcomes.append(bool(wars and captured))
            reference = turn.get('reference', {})
            offensive = reference.get('contexts', {}).get('facts', {}).get('actors', {}).get(player, {}).get('offensive', {})
            seized = [c for c in reference.get('delta', {}).get('changes', []) if c['kind'] == 'territory'
                      and c['before']['controller'] != player and c['after']['controller'] == player]
            if len(seized) > offensive.get('can_take_this_month', 0) or any(c['id'] not in offensive.get('regions', []) for c in seized):
                issues.append('Adjudicated invasion exceeded recorded offensive capacity/reach')
            if any(r['owner'] != initial['territories'][rid]['owner'] for rid, r in state['territories'].items()
                   if initial['territories'][rid]['owner'] in (player, 'ES')):
                issues.append('Invasion changed legal ownership without a signed cession')
        elif name == 'multiparty':
            p = state['proposals'].get(scenario['checks']['proposal'], {})
            outcomes.append(p.get('answers', {}).get(player, {}).get('accept') is True and
                            p.get('status') in ('pending', 'accepted', 'declined', 'expired'))
        elif name == 'regional':
            region = scenario['checks']['region']
            projects = state['actors'][player]['status'].get('projects', {})
            outcomes.append(any(p.get('target') == region and p.get('benefit') == 'infrastructure' and
                                p.get('paid', 0) > 0 for p in projects.values()))
    if name == 'access_ceasefire' and turns:
        proposals = turns[-1]['state']['proposals'].values()
        outcomes = [any(p.get('kind') == 'ceasefire' and set((player, 'ES')).issubset(p.get('actors', [])) for p in proposals),
                    any(any(t.get('action') == 'grant_access' and t.get('to') == player for t in p.get('terms', [])) for p in proposals)]
    if not outcomes or not all(outcomes):
        issues.append(MISSING_OUTCOME)
    return dict(passed=not issues, outcomes=outcomes, issues=issues)


def check_attribution(months):
    issues = []
    for month in months:
        for audit in month.get('inference', []):
            if audit.get('kind') != 'adjudicate' or audit.get('status') != 'success':
                continue
            intentions = audit.get('input', {}).get('intentions', [])
            events = audit.get('answer', {}).get('events', [])
            if len(events) != len(intentions):
                issues.append('Outcome count does not match intentions')
                continue
            for event, intent in zip(events, intentions):
                words = list(re.finditer(r'[^ ]+', intent['action']))
                end = words[5].end() if len(words) >= 6 else len(intent['action'])
                opening = intent['action'][:end] + ('…' if end < len(intent['action']) else '')
                if event.get('actor') != intent['actor'] or event.get('action') != opening:
                    issues.append('Outcome attribution mismatch')
                if intent.get('regional_actor'):
                    ops = event.get('operations', [])
                    if len(ops) != 1 or ops[0].get('action') != 'regional_initiative' or ops[0].get('region') != intent['regional_actor']:
                        issues.append('Regional scope mismatch')
    return issues


def inference_totals(months):
    audits = [a for month in months for a in month.get('inference', []) if a.get('kind') != 'turn_timing']
    return dict(attempts=len(audits), retries=sum(bool(a.get('retry_reason')) for a in audits),
                failed_attempts=sum(a.get('status') != 'success' for a in audits),
                truncations=sum('truncat' in str(a.get('status', '')).lower() or
                                'truncat' in str(a.get('error', '')).lower() for a in audits),
                prompt_tokens=sum(a.get('prompt_tokens') or 0 for a in audits),
                output_tokens=sum(a.get('output_tokens') or 0 for a in audits),
                reasoning_tokens=sum(a['reasoning_tokens'] for a in audits if a.get('reasoning_tokens') is not None),
                reasoning_counts_unavailable=sum(a.get('reasoning_tokens') is None for a in audits),
                reasoning_token_provenance='post-turn tokenizer, excludes reasoning delimiters; raw reasoning retained',
                elapsed_attempt_ms=sum(a.get('latency_ms') or 0 for a in audits),
                failed_attempt_ms=sum(a.get('latency_ms') or 0 for a in audits if a.get('status') != 'success'))
