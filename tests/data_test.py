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
print('Data normalization, city separation, missing-code handling and flags passed')
