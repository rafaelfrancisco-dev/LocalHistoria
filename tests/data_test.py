import json, sqlite3, pathlib, sys
root=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from import_data import normalize
f=json.loads((root/'web/world.geojson').read_text())['features']
assert len({x['id'] for x in f})==len(f)==258
# Browser display geometry stays small, keeps every region and ships a byte-identical gzip for gzip_static.
import gzip
topology=root/'web/regions.topojson'
assert topology.stat().st_size<12_000_000,topology.stat().st_size
for path in (topology,root/'web/world.geojson'):assert gzip.decompress(path.with_name(path.name+'.gz').read_bytes())==path.read_bytes(),path
display=[g['id'] for g in json.loads(topology.read_text())['objects']['regions']['geometries']]
for name,code in [('France','FR'),('Norway','NO'),('Kosovo','XK'),('Taiwan','TW')]:
 assert normalize({'properties':{'name':name,'ISO3166-1-Alpha-2':'-99'}},0)['properties']['code']==code
assert not normalize({'properties':{'name':'Unresolved'}},5)['properties']['playable']
assert any(not x['properties']['playable'] for x in f)
db=sqlite3.connect(root/'data/world.sqlite')
assert len(display)==len(set(display))==db.execute('SELECT count(*) FROM regions').fetchone()[0]
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
# Military inventories are independently sourced; observations keep their own years.
db.row_factory=sqlite3.Row
military={r['code']:dict(r) for r in db.execute('SELECT * FROM military_stats')}
assert set(military)==set(stats)|set(seed['actors'])
curated=json.loads((root/'data/source/military_2026.json').read_text())['countries']
assert len(curated)>=70
for code,m in military.items():
 assert all(isinstance(m[k],int) and m[k]>=0 for k in ('active','reserve','ships','aircraft','spend_usd')),m
 assert m['source'] and m['estimated'] in (0,1)
 assert m['active_year']>=0 and m['spend_year']>=0
references={'US':(1330000,800000,240,2700),'CN':(2035000,510000,200,1900),'IN':(1455000,1155000,47,640),'RU':(1320000,2000000,90,1500),'KR':(500000,3100000,50,470),'KP':(1280000,600000,75,450),'PK':(660000,550000,25,400),'UA':(900000,1200000,0,100),'IR':(610000,350000,25,330),'IL':(170000,465000,12,340),'ES':(133000,15000,14,140),'PT':(28000,210000,7,27),'MG':(13500,0,0,0),'KM':(1000,0,0,0)}
for c,values in references.items():
 for field,expected in zip(('active','reserve','ships','aircraft'),values):assert abs(military[c][field]-expected)<=expected*.15,(c,field,military[c][field],expected)
 assert military[c]['estimated']==1 # Scenario overrides are never disguised as World Bank observations.
assert military['PT']['active_year']==2025 and military['PT']['spend_year']==2024
assert curated['PT']['aircraft_observed_2025']==21 and curated['IN']['aircraft_observed_2025']==643
assert len(seed['conflicts'][0]['entities'])==4
assert all(e['region'] in regions and e['personnel']>0 for e in seed['conflicts'][0]['entities'])
from import_stats import military as import_military
# Known zeros remain zeros; missing fields use the documented fallbacks, including non-state actors.
fallback=import_military(['ZZ'],{'ZZ':{'population':10000}},{'actors':{}})['ZZ']
assert (fallback['active'],fallback['reserve'],fallback['ships'],fallback['aircraft'],fallback['estimated'])==(200,100,0,0,1)
assert military['MG']['reserve']==0 and military['rapid-support-forces']['active']==100000
print('Military references, provenance, fallbacks, observed aircraft and seeded formations passed')
