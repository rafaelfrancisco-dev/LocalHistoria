#!/usr/bin/env python3
"""Regression: copying a live WAL must include committed turns and exclude uncommitted ones."""
import pathlib
import sqlite3
import sys
import tempfile
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from benchmark_fixture import backup
from adjudication_compare import summarize, adoption_gate
from adjudication_quality import check_outcomes, check_attribution


class FixtureTests(unittest.TestCase):
    def test_live_wal_snapshot(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, target = pathlib.Path(tmp)/'source.sqlite', pathlib.Path(tmp)/'copy.sqlite'
            with sqlite3.connect(source) as db:
                db.execute('PRAGMA journal_mode=WAL')
                db.execute('CREATE TABLE turns(turn INTEGER)')
                db.execute('INSERT INTO turns VALUES(1)')
                db.commit()
                db.execute('INSERT INTO turns VALUES(2)')
                self.assertEqual(backup(source, target), {'turns': 1})
                with sqlite3.connect(target) as copied:
                    self.assertEqual(copied.execute('SELECT * FROM turns').fetchall(), [(1,)])
                with self.assertRaises(ValueError):
                    backup(source, target)
                with self.assertRaises(ValueError):
                    backup(source, source)

    def test_failed_attempts_are_not_fast_successes(self):
        trials = [dict(scenario='peace', arm='compact', returncode=1, months=[
            dict(job=dict(status='complete'), seconds_to_commit=100),
            dict(job=dict(status='probe_failed'), elapsed_seconds=210)])]
        summary = summarize(trials)['peace/compact']
        self.assertEqual(summary['median'], 100)
        self.assertEqual(summary['successful_months'], 1)
        self.assertEqual(summary['failed_runs'], 1)
        self.assertEqual(summary['tail_evidence'], 'insufficient')

    def test_faster_wrong_turns_cannot_pass_adoption(self):
        rows = []
        for repeat in range(3):
            for arm, seconds in [('baseline', 100), ('compact', 50)]:
                rows.append(dict(scenario='peace', arm=arm, repeat=repeat, returncode=0,
                                 quality=dict(passed=arm == 'baseline'), months=[
                                     dict(job=dict(status='complete'), seconds_to_commit=seconds)]*2))
        result = adoption_gate(rows, 'compact', ['peace'], 3)
        self.assertFalse(result['mac_passed'])
        self.assertFalse(result['enable_default'])
        self.assertIn('peace: candidate lost required outcomes the baseline achieved', result['reasons'])

    def test_shared_baseline_miss_is_noted_not_rejected(self):
        rows = []
        for repeat in range(3):
            for arm, seconds in [('baseline', 100), ('compact', 80)]:
                rows.append(dict(scenario='peace', arm=arm, repeat=repeat, returncode=0,
                                 quality=dict(passed=False, issues=['Required scenario outcome missing']), months=[
                                     dict(job=dict(status='complete'), seconds_to_commit=seconds)]*2))
        result = adoption_gate(rows, 'compact', ['peace'], 3)
        self.assertTrue(result['mac_passed'])
        self.assertIn('peace: baseline missed required outcomes in 3/3 runs', result['baseline_notes'])
        rows[1]['quality']['issues'].append('Unauthorized signature: p')
        self.assertIn('peace: failed candidate run or hard quality violation',
                      adoption_gate(rows, 'compact', ['peace'], 3)['reasons'])

    def test_wrong_revision_and_missing_project_fail(self):
        state = dict(player='PT', actors={'PT': {'status': {}}}, territories={}, regional_politics={},
                     proposals={'p': dict(status='accepted', revision=2, actors=['PT'],
                                          answers={'PT': dict(accept=True, revision=1)})})
        result = check_outcomes(dict(name='peace'), dict(initial_state=state, turns=[dict(state=state)]), 1)
        self.assertFalse(result['passed'])
        self.assertIn('Unauthorized signature: p', result['issues'])
        self.assertEqual(result['outcomes'], [False])

    def test_attribution_checks_regional_scope(self):
        audit = dict(kind='adjudicate', status='success', input=dict(intentions=[
            dict(actor='PT', action='Fund local schools', regional_actor='r1')]),
            answer=dict(events=[dict(actor='PT', action='Fund local schools',
                                     operations=[dict(action='regional_initiative', region='r2')])]))
        self.assertEqual(check_attribution([dict(inference=[audit])]), ['Regional scope mismatch'])


if __name__ == '__main__':
    unittest.main()
