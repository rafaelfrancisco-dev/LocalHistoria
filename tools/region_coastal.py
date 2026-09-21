"""Mark regions that meet the sea, so naval strength applies where a fleet could actually reach.

A region is coastal when it lies within TOL of the outline of the world's landmass, which is the
union of the country polygons in countries.geojson. Interior borders dissolve in that union, so
what remains of its boundary is coastline. The tolerance absorbs the misalignment between gbOpen
ADM1 boundaries and the country outlines, which come from different sources.

Large inland seas count as coast. That is deliberate: Caspian and Great Lakes flotillas are real.

The coastline is cut into short chunks before indexing. A single ring wraps all of Eurasia and
Africa, and its bounding box covers half the world, so an untouched ring defeats the spatial index
and every region ends up in a full distance test against millions of vertices.
"""
import json, pathlib, sqlite3, time
from shapely.geometry import shape, LineString
from shapely.ops import unary_union
from shapely import STRtree

TOL = 0.05   # degrees, about 5 km
CHUNK = 200  # coastline vertices per indexed piece
ROOT = pathlib.Path(__file__).resolve().parents[1]

def chunks(rings):
    """Split each ring into overlapping short lines so their bounding boxes stay tight."""
    for ring in rings:
        pts = list(ring.coords)
        for i in range(0, len(pts) - 1, CHUNK):
            piece = pts[i:i + CHUNK + 1]  # +1 so consecutive chunks share a vertex, leaving no gap
            if len(piece) > 1:
                yield LineString(piece)

def main():
    start = time.time()
    feats = json.loads((ROOT / 'data/source/countries.geojson').read_text())['features']
    land = unary_union([shape(f['geometry']).buffer(0) for f in feats])
    coast = land.boundary
    rings = [coast] if coast.geom_type == 'LineString' else list(coast.geoms)
    pieces = list(chunks(rings))
    tree = STRtree(pieces)

    db = sqlite3.connect(ROOT / 'data/world.sqlite')
    rows = db.execute('SELECT id,geometry FROM regions ORDER BY id').fetchall()
    coastal = []
    for n, (rid, geom) in enumerate(rows, 1):
        g = shape(json.loads(geom))
        near = tree.query(g, predicate='dwithin', distance=TOL)
        coastal.append((1 if len(near) else 0, rid))
        if n % 400 == 0:
            print(f'  {n}/{len(rows)} regions, {time.time()-start:.0f}s', flush=True)

    db.executemany('UPDATE regions SET coastal=? WHERE id=?', coastal)
    db.commit()
    print(f'Marked {sum(c for c, _ in coastal)} of {len(coastal)} regions coastal '
          f'from {len(pieces)} coastline chunks in {time.time()-start:.0f}s', flush=True)

main()
