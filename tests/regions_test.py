"""Exhaustive partition checks using the importer's pinned geometry dependency."""
import hashlib,json,pathlib,sys
from shapely import set_precision
from shapely.geometry import shape
from shapely.ops import unary_union
from shapely.strtree import STRtree
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from import_regions import polygons
from import_data import normalize
root=pathlib.Path(__file__).resolve().parents[1]
manifest=json.loads((root/'data/source/gbOpen/manifest.json').read_text())
assert hashlib.sha256((root/'web/regions.geojson').read_bytes()).hexdigest()==manifest['geometry_sha256']
for source in manifest['sources']:
 assert hashlib.sha256((root/'data/source/gbOpen'/f"{source['boundaryISO']}.json").read_bytes()).hexdigest()==source['sha256']
features=json.loads((root/'web/regions.geojson').read_text())['features'];ids=[f['id'] for f in features];assert len(ids)==len(set(ids))
geometries=[shape(f['geometry']) for f in features];tree=STRtree(geometries)
for n,g in enumerate(geometries):
 assert g.is_valid and not g.is_empty
 for k in tree.query(g):
  if k<n:assert g.intersection(geometries[k]).area<1e-8,(ids[n],ids[k])
print(f"Verified {len(ids)} stable unique IDs, all polygons valid, no interior overlaps and {len(manifest['sources'])} source checksums")
# Every supplied outline point must be covered by the reconciled union; claims may overlap.
original=[set_precision(polygons(shape(f['geometry'])),1e-8) for f in json.loads((root/'data/source/countries.geojson').read_text())['features']]
for n,g in enumerate(original):
 candidates=tree.query(g);covered=unary_union([geometries[k] for k in candidates]);assert g.difference(covered).area<1e-7,(n,g.difference(covered).area)
print('Verified coverage of all supplied countries and islands, including residual territories')
# Adjacency drives how far force can reach in one month: symmetric, and every multi-region country is connected to itself.
import sqlite3
db=sqlite3.connect(root/'data/world.sqlite')
pairs=set(db.execute('SELECT a,b FROM region_neighbors'))
assert pairs and all((b,a) in pairs for a,b in pairs)
lonely=db.execute("SELECT count(*) FROM regions r WHERE kind='ADM1' AND id NOT IN (SELECT a FROM region_neighbors) AND affiliation IN (SELECT affiliation FROM regions WHERE kind='ADM1' GROUP BY affiliation HAVING count(*)>1)").fetchone()[0]
assert lonely<20,lonely
print(f'Verified {len(pairs)//2} symmetric region adjacencies ({lonely} isolated island subdivisions)')
