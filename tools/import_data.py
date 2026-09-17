#!/usr/bin/env python3
"""Rebuild geography and SQLite reference data from project-owned inputs."""
import gzip, json, re, sqlite3, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[1]
OVERRIDES = {'France':'FR','Norway':'NO','Kosovo':'XK','Taiwan':'TW'}
def normalize(feature, index):
    p = feature['properties']; code = OVERRIDES.get(p['name'], p.get('ISO3166-1-Alpha-2',''))
    code = code if re.fullmatch('[A-Z]{2}',code) else None
    return dict(feature, id=f'feature-{index:03d}', properties={'name':p['name'],'code':code,'playable':bool(code)})
def main():
    raw=json.loads((ROOT/'data/source/countries.geojson').read_text())
    features=[normalize(f,i) for i,f in enumerate(raw['features'])]
    (ROOT/'web/world.geojson').write_text(json.dumps({'type':'FeatureCollection','features':features},separators=(',',':')))
    path=ROOT/'data/world.sqlite'; path.unlink(missing_ok=True)
    db=sqlite3.connect(path)
    db.executescript('CREATE TABLE countries(code TEXT PRIMARY KEY,name TEXT NOT NULL,country_wikidata TEXT); CREATE TABLE cities(id INTEGER PRIMARY KEY,code TEXT,name TEXT,population INTEGER,latitude REAL,longitude REAL,city_wikidata TEXT); CREATE INDEX city_country ON cities(code,population DESC);')
    for f in features:
        p=f['properties']
        if p['code']: db.execute('INSERT OR IGNORE INTO countries VALUES(?,?,NULL)',(p['code'],p['name']))
    with gzip.open(ROOT/'data/source/json-cities.json.gz','rt') as f: cities=json.load(f)
    db.executemany('INSERT OR IGNORE INTO cities VALUES(?,?,?,?,?,?,?)',((c['id'],c['country_code'],c['name'],c.get('population'),c['latitude'],c['longitude'],c.get('wikiDataId')) for c in cities))
    db.commit(); print(f'Imported {len(features)} map features, {db.execute("SELECT count(*) FROM countries").fetchone()[0]} actors, {len(cities)} cities')
if __name__=='__main__': main()
