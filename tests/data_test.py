import json, sqlite3, pathlib, sys
root=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from import_data import normalize
f=json.loads((root/'web/world.geojson').read_text())['features']
assert len({x['id'] for x in f})==len(f)==258
for name,code in [('France','FR'),('Norway','NO'),('Kosovo','XK'),('Taiwan','TW')]:
 assert normalize({'properties':{'name':name,'ISO3166-1-Alpha-2':'-99'}},0)['properties']['code']==code
assert not normalize({'properties':{'name':'Unresolved'}},5)['properties']['playable']
assert any(not x['properties']['playable'] for x in f)
db=sqlite3.connect(root/'data/world.sqlite')
assert db.execute('SELECT count(*) FROM cities').fetchone()[0]==152970
assert db.execute('SELECT count(*) FROM countries WHERE country_wikidata IS NOT NULL').fetchone()[0]==0
assert db.execute('SELECT city_wikidata FROM cities WHERE city_wikidata IS NOT NULL LIMIT 1').fetchone()[0].startswith('Q')
missing=[c for c, in db.execute('SELECT code FROM countries') if not (root/'web/flags'/f'{c.lower()}.svg').exists()]
assert not missing,missing
assert 'population' not in [r[1] for r in db.execute('PRAGMA table_info(countries)')]
# National statistics: every actor has World Bank data or a sourced estimate, and regional shares sum to national totals.
stats={r[0]:r for r in db.execute('SELECT code,population,gdp_usd,stability,currency,usd_rate,estimated,source FROM country_stats')}
assert set(stats)=={c for c, in db.execute('SELECT code FROM countries')}
for code,population,gdp,stability,currency,rate,estimated,source in stats.values():
 assert population>=0 and gdp>=0 and 0<=stability<=100 and len(currency)==3 and rate>0 and source,(code,population,gdp,stability,currency,rate)
 assert not estimated or code in json.loads((root/'data/source/stats_overrides.json').read_text())['countries'],code
assert 10_000_000<stats['PT'][1]<11_500_000 and stats['PT'][4]=='EUR' and stats['BG'][4]=='EUR' and stats['US'][5]==1
mismatched=db.execute("SELECT c.code FROM country_stats c JOIN (SELECT r.affiliation code,SUM(s.population) p,SUM(s.gdp_usd) g FROM region_stats s JOIN regions r ON r.id=s.id GROUP BY r.affiliation) t ON t.code=c.code WHERE t.p!=c.population OR t.g!=c.gdp_usd").fetchall()
assert not mismatched,mismatched
seed=json.loads(db.execute('SELECT document FROM world_seed').fetchone()[0]);regions={r for r, in db.execute('SELECT id FROM regions')}
assert {'Russo-Ukrainian War','Sudanese Civil War'}<={c['name'] for c in seed['conflicts']} and 'North Atlantic Treaty Organization' in {c['name'] for c in seed['commitments']}
for c in seed['conflicts']:assert set(c['affected_regions'])<=regions and all(set(v)<=regions for v in c['occupied'].values())
print('Data normalization, city separation, missing-code handling, flags, national statistics and the 2026 world seed passed')
