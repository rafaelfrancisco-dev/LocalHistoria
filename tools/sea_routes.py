"""Sea routes between coastal regions: where a fleet can sail within a month, and how far.

A coastal flag says a region meets water, not which water. The Caspian, the Black Sea and the
Baltic are all coast, and a straight line between two coastal points can cross a continent. So
each coastal region gets anchor points along its own shoreline, and a mesh of offshore waypoints
lets routes round capes that no straight shore-to-shore hop can. Anchors and waypoints are linked by
straight hops that stay clear of land; the shortest chain of hops between two regions is their route.

Land is the union of the country polygons, eroded by EROSION before testing a hop. That keeps a
hop that grazes a shoreline (both anchors sit on one) from counting as crossing land, and it
opens straits narrower than twice EROSION: Gibraltar, the Bosporus and Dardanelles, the Oresund,
Kerch. It also opens isthmuses as narrow, such as Corinth and Perekop, which is the price of a
coarse graph. Wider isthmuses stay closed, so the two canals that matter are added explicitly.

A fleet in a region on two seas can sail from any of its anchors, since the region is its
smallest known position; a route passing through a region never jumps between its anchors. So a
fleet based in Colombia's Choco reaches both oceans, but no fleet crosses Choco to get from one
to the other. That is the documented limit of region granularity.

Writes sea_routes(a, b, km) for every ordered pair within LIMIT_KM; sea_basins(region, basin), the
bodies of water a supply line can cross at any distance; and sea_route_meta with a content revision
that campaigns record so replay uses the same graph. Rewrites only those three tables; it never
touches region geometry.
"""
import hashlib, heapq, json, math, pathlib, sqlite3, time
from collections import defaultdict
import numpy as np
import shapely
from shapely.geometry import LineString, Point, box, shape
from shapely.ops import unary_union
from shapely import STRtree

TOL = 0.05        # degrees: a shoreline this close to a region belongs to it (as region_coastal.py)
EROSION = 0.08    # degrees, about 9 km
CELL = 1.0        # degrees: at most one anchor per region per cell
MAX_HOP_KM = 400  # longest single straight hop; longer passages chain hops
LIMIT_KM = 1500   # a fleet's monthly allowance, the longest route stored
TILE = 2.0        # degrees: land is tiled so a hop only tests the land beside it
WAY = 0.5         # degrees: offshore waypoint grid
BAND = 3          # grid cells: waypoints lie within this many cells of land
CHUNK = 200
# Canals the erosion cannot open: (name, one end, other end, length in km), as lon/lat.
CANALS = [('Suez Canal', (32.31, 31.27), (32.56, 29.93), 193),
          ('Panama Canal', (-79.92, 9.37), (-79.53, 8.93), 82),
          ('Kiel Canal', (9.15, 53.89), (10.15, 54.36), 98)]
ROOT = pathlib.Path(__file__).resolve().parents[1]


def km(a, b):
    (lon1, lat1), (lon2, lat2) = a, b
    p1, p2 = math.radians(lat1), math.radians(lat2)
    h = (math.sin((p2 - p1) / 2) ** 2 +
         math.cos(p1) * math.cos(p2) * math.sin(math.radians(lon2 - lon1) / 2) ** 2)
    return 2 * 6371 * math.asin(math.sqrt(min(1.0, h)))


def chunks(rings):
    for ring in rings:
        pts = list(ring.coords)
        for i in range(0, len(pts) - 1, CHUNK):
            piece = pts[i:i + CHUNK + 1]
            if len(piece) > 1:
                yield LineString(piece)


def ocean_basins(anchors, owner, edges, land_tree):
    """Bodies of water a supply line can cross, however far: the hop graph's components, with every
    smaller one joined to another along the shortest straight line that clears land. Pacific islands
    join the Pacific this way; the Caspian finds no such line and stays apart."""
    parent = list(range(len(anchors)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i, links in edges.items():
        for j, _ in links:
            parent[find(i)] = find(j)
    ports = [i for i, o in enumerate(owner) if o is not None]
    xy = np.radians(np.array(anchors))
    lon, lat = xy[:, 0], xy[:, 1]
    while True:
        groups = defaultdict(list)
        for i in range(len(anchors)):
            groups[find(i)].append(i)
        if len(groups) == 1:
            break
        largest = max(groups, key=lambda g: (len(groups[g]), -g))
        joined = False
        # Smallest first, so an island reaches for the open ocean rather than the reverse.
        coastal = {find(i) for i in ports}
        for root, members in sorted(groups.items(), key=lambda g: (len(g[1]), g[0])):
            # Open water no coast touches supplies no one; only waters with ports need joining.
            if root == largest or find(root) != root or root not in coastal:
                continue
            outside = np.array([find(i) != root for i in range(len(anchors))])
            best = np.full(len(anchors), np.inf)
            source = np.zeros(len(anchors), dtype=int)
            for chunk in range(0, len(members), 256):
                m = np.array(members[chunk:chunk + 256])
                h = (np.sin((lat[None, :] - lat[m, None]) / 2) ** 2 + np.cos(lat[m, None]) *
                     np.cos(lat[None, :]) * np.sin((lon[None, :] - lon[m, None]) / 2) ** 2)
                d = 2 * 6371 * np.arcsin(np.sqrt(np.clip(h, 0, 1)))
                k = d.argmin(axis=0)
                closer = d[k, np.arange(len(anchors))] < best
                best = np.where(closer, d[k, np.arange(len(anchors))], best)
                source = np.where(closer, m[k], source)
            best[~outside] = np.inf
            order = [j for j in np.argsort(best, kind='stable')[:2000] if np.isfinite(best[j])]
            if not order:
                continue
            lines = shapely.linestrings([[anchors[source[j]], anchors[j]] for j in order])
            blocked = set(land_tree.query(lines, predicate='intersects')[0].tolist())
            for n, j in enumerate(order):
                if n not in blocked:
                    parent[find(root)] = find(int(j))
                    joined = True
                    break
        if not joined:
            break
    roots = {}
    for i in ports:
        roots.setdefault(find(i), len(roots))
    return sorted({(owner[i], roots[find(i)]) for i in ports})


def main():
    start = time.time()
    feats = json.loads((ROOT / 'data/source/countries.geojson').read_text())['features']
    land = unary_union([shape(f['geometry']).buffer(0) for f in feats])
    coast = land.boundary
    rings = [coast] if coast.geom_type == 'LineString' else list(coast.geoms)
    pieces = list(chunks(rings))
    coast_tree = STRtree(pieces)
    print(f'land and {len(pieces)} coastline chunks in {time.time()-start:.0f}s', flush=True)

    # Eroded land, tile by tile: eroding the whole union at once is slow and memory hungry.
    tiles = []
    for x in np.arange(-180, 180, TILE):
        for y in np.arange(-90, 90, TILE):
            cell = box(x, y, x + TILE, y + TILE)
            local = shapely.clip_by_rect(land, x - 2 * EROSION, y - 2 * EROSION,
                                         x + TILE + 2 * EROSION, y + TILE + 2 * EROSION)
            if local.is_empty:
                continue
            eroded = local.buffer(-EROSION).intersection(cell)
            if not eroded.is_empty:
                tiles.append(eroded)
    land_tree = STRtree(tiles)
    print(f'{len(tiles)} eroded land tiles in {time.time()-start:.0f}s', flush=True)

    db = sqlite3.connect(ROOT / 'data/world.sqlite')
    rows = db.execute('SELECT id, geometry FROM regions WHERE coastal = 1 ORDER BY id').fetchall()
    anchors, owner = [], []
    for rid, geom in rows:
        g = shape(json.loads(geom))
        near = coast_tree.query(g, predicate='dwithin', distance=TOL)
        if not len(near):
            continue
        shore = unary_union([pieces[i] for i in near]).intersection(g.buffer(TOL))
        parts = [p for p in getattr(shore, 'geoms', [shore]) if p.geom_type == 'LineString']
        seen = set()
        for part in parts:
            n = max(1, int(part.length / (CELL / 2)))
            for k in range(n + 1):
                p = part.interpolate(k / n, normalized=True)
                cell = (math.floor(p.x / CELL), math.floor(p.y / CELL))
                if cell in seen:
                    continue
                seen.add(cell)
                anchors.append((p.x, p.y))
                owner.append(rid)
    print(f'{len(anchors)} anchors on {len(set(owner))} coastal regions '
          f'in {time.time()-start:.0f}s', flush=True)

    # Offshore waypoints: water on a WAY-degree grid within BAND cells of land. Anchors sit on the
    # shore, so a hop past a cape needs an anchor at its very tip; the waypoints let a route round it.
    xs = np.arange(-180 + WAY / 2, 180, WAY)
    ys = np.arange(-90 + WAY / 2, 90, WAY)
    gx_, gy_ = np.meshgrid(xs, ys)
    shapely.prepare(land)
    onland = shapely.contains_xy(land, gx_.ravel(), gy_.ravel()).reshape(gx_.shape)
    near = np.zeros_like(onland)
    for dy in range(-BAND, BAND + 1):
        for dx in range(-BAND, BAND + 1):
            near |= np.roll(np.roll(onland, dy, axis=0), dx, axis=1)
    water = near & ~onland
    first = len(anchors)
    cell_node = {}
    for iy, ix in zip(*np.nonzero(water)):
        cell_node[(int(iy), int(ix))] = len(anchors)
        anchors.append((float(xs[ix]), float(ys[iy])))
        owner.append(None)
    print(f'{len(anchors) - first} offshore waypoints in {time.time()-start:.0f}s', flush=True)

    # Candidate hops by a coarse grid, then one vectorised land test for all of them.
    hop_deg = MAX_HOP_KM / 111.0
    grid = defaultdict(list)
    for i, (x, y) in enumerate(anchors[:first]):
        grid[(math.floor(x / hop_deg), math.floor(y / hop_deg))].append(i)
    pairs = []
    for (iy, ix), i in cell_node.items():
        for dy, dx in ((0, 1), (1, -1), (1, 0), (1, 1)):
            j = cell_node.get((iy + dy, (ix + dx) % len(xs)))
            if j is not None:
                pairs.append((i, j))
    for i, (x, y) in enumerate(anchors[:first]):
        ix, iy = int((x + 180) // WAY), int((y + 90) // WAY)
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                j = cell_node.get((iy + dy, (ix + dx) % len(xs)))
                if j is not None:
                    pairs.append((i, j))
    for i, (x, y) in enumerate(anchors[:first]):
        gx, gy = math.floor(x / hop_deg), math.floor(y / hop_deg)
        span = 1 + int(1 / max(0.2, math.cos(math.radians(y))))
        for dx in range(-span, span + 1):
            for dy in (-1, 0, 1):
                for j in grid.get((gx + dx, gy + dy), ()):
                    if j > i and km(anchors[i], anchors[j]) <= MAX_HOP_KM:
                        pairs.append((i, j))
    lines = shapely.linestrings([[anchors[i], anchors[j]] for i, j in pairs])
    hits = land_tree.query(lines, predicate='intersects')
    blocked = set(hits[0].tolist())
    edges = defaultdict(list)
    for n, (i, j) in enumerate(pairs):
        if n not in blocked:
            d = km(anchors[i], anchors[j])
            edges[i].append((j, d))
            edges[j].append((i, d))
    print(f'{len(pairs) - len(blocked)} of {len(pairs)} hops clear of land '
          f'in {time.time()-start:.0f}s', flush=True)

    by_region = defaultdict(list)
    for i, rid in enumerate(owner[:first]):
        by_region[rid].append(i)
    for name, a, b, length in CANALS:
        ia = min(range(first), key=lambda i: km(anchors[i], a))
        ib = min(range(first), key=lambda i: km(anchors[i], b))
        edges[ia].append((ib, length))
        edges[ib].append((ia, length))
        print(f'{name}: {owner[ia]} <-> {owner[ib]}', flush=True)

    routes = {}
    for rid, sources in by_region.items():
        dist = {i: 0.0 for i in sources}
        heap = [(0.0, i) for i in sources]
        best = {}
        while heap:
            d, i = heapq.heappop(heap)
            if d > dist.get(i, math.inf):
                continue
            o = owner[i]
            if o is not None and o != rid and d < best.get(o, math.inf):
                best[o] = d
            for j, w in edges[i]:
                nd = d + w
                if nd <= LIMIT_KM and nd < dist.get(j, math.inf):
                    dist[j] = nd
                    heapq.heappush(heap, (nd, j))
        for o, d in best.items():
            routes[(rid, o)] = max(1, int(round(d)))
    print(f'{len(routes)} routes within {LIMIT_KM} km in {time.time()-start:.0f}s', flush=True)

    basins = ocean_basins(anchors, owner, edges, land_tree)
    print(f'{len(set(b for _, b in basins))} ocean basins in {time.time()-start:.0f}s', flush=True)

    rows = sorted((a, b, d) for (a, b), d in routes.items())
    digest = hashlib.sha256(json.dumps(
        {'params': [TOL, EROSION, CELL, MAX_HOP_KM, LIMIT_KM, WAY, BAND, CANALS], 'routes': rows,
         'basins': basins}
    ).encode()).hexdigest()[:16]
    db.execute('DROP TABLE IF EXISTS sea_routes')
    db.execute('DROP TABLE IF EXISTS sea_route_meta')
    db.execute('DROP TABLE IF EXISTS sea_basins')
    db.execute('CREATE TABLE sea_basins(region TEXT, basin INTEGER NOT NULL, PRIMARY KEY(region, basin))')
    db.executemany('INSERT INTO sea_basins VALUES(?,?)', basins)
    db.execute('CREATE TABLE sea_routes(a TEXT, b TEXT, km INTEGER NOT NULL, PRIMARY KEY(a, b))')
    db.execute('CREATE TABLE sea_route_meta(revision TEXT NOT NULL)')
    db.executemany('INSERT INTO sea_routes VALUES(?,?,?)', rows)
    db.execute('INSERT INTO sea_route_meta VALUES(?)', (digest,))
    db.commit()
    print(f'Sea route revision {digest}', flush=True)


main()
