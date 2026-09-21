"""Region adjacency for realistic monthly movement: touching regions, or regions across a narrow strait."""
import json,pathlib,sqlite3
from shapely.geometry import shape
from shapely.strtree import STRtree
GAP=0.05
root=pathlib.Path(__file__).resolve().parents[1]
db=sqlite3.connect(root/'data/world.sqlite')
rows=db.execute('SELECT id,geometry FROM regions ORDER BY id').fetchall()
ids=[r[0] for r in rows];shapes=[shape(json.loads(r[1])) for r in rows];tree=STRtree(shapes)
db.executescript('DROP TABLE IF EXISTS region_neighbors; CREATE TABLE region_neighbors(a TEXT,b TEXT,PRIMARY KEY(a,b));')
pairs=set()
# dwithin means exactly `g.distance(other)<=GAP`, but GEOS prunes inside the tree and stops as soon
# as the answer is known, instead of buffering every polygon and measuring each candidate in full.
for i,g in enumerate(shapes):
 for k in tree.query(g,predicate='dwithin',distance=GAP):
  if k!=i:pairs.add((ids[i],ids[k]));pairs.add((ids[k],ids[i]))
db.executemany('INSERT INTO region_neighbors VALUES(?,?)',sorted(pairs))
db.commit()
print(f'Indexed {len(pairs)//2} region adjacencies',flush=True)
