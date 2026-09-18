#!/usr/bin/env python3
"""Browser display geometry, derived from the authoritative geometry in data/world.sqlite.

Regions become one TopoJSON topology: shared borders are stored once as arcs, so simplification cannot open
gaps between neighbours and the browser can mesh national borders from current ownership. The pre-campaign
country outlines stay GeoJSON. Both are written with a deterministic .gz beside them for gzip_static serving.
"""
import gzip, hashlib, json, pathlib, sqlite3, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
REGION_TOLERANCE=0.01  # degrees, applied to shared arcs after the topology is built
WORLD_TOLERANCE=0.02
QUANTIZATION=1e6  # grid steps across the bounding box, about 40 m at world scale
NODING_GRID=1e-6  # degrees; snap-rounding grid that makes near-coincident borders identical

def geometry_digest(db):
    """Checksum of the authoritative region geometry, in import order."""
    h=hashlib.sha256()
    for id,geometry in db.execute('SELECT id,geometry FROM regions ORDER BY rowid'):h.update(f'{id}\n{geometry}\n'.encode())
    return h.hexdigest()

def write(path,data):
    """Writes data and a byte-identical-on-rebuild .gz, returning the sha256 of the uncompressed file."""
    path.write_bytes(data);path.with_name(path.name+'.gz').write_bytes(gzip.compress(data,9,mtime=0))
    return hashlib.sha256(data).hexdigest()

def topology(features,tolerance,quantization=QUANTIZATION):
    """TopoJSON for (id, Polygon/MultiPolygon) pairs, following topojson-server: find junctions on the fine noding
    grid, cut rings into arcs, store each shared arc once, simplify every arc once so neighbours stay identical, and
    only then quantize for output (quantizing first turns dense coastlines into spikes that all become junctions)."""
    import numpy as np, shapely
    polygons=[[[np.asarray(r,dtype=float)[:,:2] for r in p] for p in (g['coordinates'] if g['type']=='MultiPolygon' else [g['coordinates']])] for _,g in features]
    every=np.concatenate([r for f in polygons for p in f for r in p])
    (x0,y0),(x1,y1)=every.min(0),every.max(0);kx,ky=(x1-x0)/(quantization-1),(y1-y0)/(quantization-1)
    shift=np.int64(1)<<30
    def quantize(r):
        q=np.rint((r-(x0,y0))/NODING_GRID).astype(np.int64);code=q[:,0]*shift+q[:,1]
        code=code[np.concatenate(([True],code[1:]!=code[:-1]))]
        if code[0]==code[-1]:code=code[:-1]
        return code  # open ring, no repeated consecutive points
    rings=[[[quantize(r) for r in p] for p in f] for f in polygons]
    # A junction is a point reached with different neighbours by two rings (or twice by one): arcs end there.
    opened=[r for f in rings for p in f for r in p if len(r)>=3]
    points=np.concatenate(opened);prev=np.concatenate([np.roll(r,1) for r in opened]);next=np.concatenate([np.roll(r,-1) for r in opened])
    low,high=np.minimum(prev,next),np.maximum(prev,next);order=np.lexsort((high,low,points))
    p,l,h=points[order],low[order],high[order]
    differs=(p[1:]==p[:-1])&((l[1:]!=l[:-1])|(h[1:]!=h[:-1]))
    junctions=set(p[1:][differs].tolist())
    arcs=[];index={}
    def arc(sequence):
        key=tuple(sequence)
        if key in index:return index[key]
        back=key[::-1]
        if back in index:return ~index[back]
        index[key]=len(arcs);arcs.append(np.asarray(key,dtype=np.int64));return index[key]
    def cut(ring):
        at=[i for i,c in enumerate(ring.tolist()) if c in junctions]
        if not at:
            # A ring without junctions is one closed arc; rotate both directions to their smallest point to dedupe.
            forward=np.roll(ring,-int(ring.argmin()));backward=forward[::-1];backward=np.roll(backward,-int(backward.argmin()))
            if tuple(backward.tolist())<tuple(forward.tolist()):return [~arc(np.append(backward,backward[0]).tolist())]
            return [arc(np.append(forward,forward[0]).tolist())]
        ring=np.roll(ring,-at[0]);closed=np.append(ring,ring[0]);ends=[i-at[0] for i in at]+[len(ring)]
        return [arc(closed[a:b+1].tolist()) for a,b in zip(ends,ends[1:])]
    # Rings narrower than the quantization grid vanish; a polygon goes with its exterior ring.
    shaped=[[[cut(r) for r in p if len(r)>=3] for p in f if len(p[0])>=3] for f in rings]
    def degrees(a):return np.column_stack((x0+(a//shift)*NODING_GRID,y0+(a%shift)*NODING_GRID))
    def simplify(a):
        line=shapely.linestrings(degrees(a))
        closed=a[0]==a[-1]
        s=shapely.simplify(shapely.linearrings(degrees(a)) if closed and len(a)>=4 else line,tolerance,preserve_topology=bool(closed))
        c=shapely.get_coordinates(s);q=np.rint((c-(x0,y0))/NODING_GRID).astype(np.int64)
        out=q[:,0]*shift+q[:,1]
        return a if len(out)<(4 if closed else 2) else out
    simplified=[simplify(a) for a in arcs]
    # A ring whose arcs all straightened to their junctions would collapse; its arcs keep full detail instead.
    def size(ring):return sum(len(simplified[i if i>=0 else ~i])-1 for i in ring)
    for f in shaped:
        for p in f:
            for ring in p:
                if size(ring)<3:
                    for i in ring:simplified[i if i>=0 else ~i]=arcs[i if i>=0 else ~i]
    encoded=[]
    for a in simplified:
        xy=np.rint((degrees(a)-(x0,y0))/(kx,ky)).astype(np.int64)
        kept=xy[np.concatenate(([True],(xy[1:]!=xy[:-1]).any(1)))]
        xy=kept if len(kept)>=2 else xy[[0,-1]]
        xy[1:]-=xy[:-1].copy();encoded.append(xy.tolist())
    geometries=[]
    for (id,_),f in zip(features,shaped):
        if not f:geometries.append({'type':None,'id':id})
        elif len(f)==1:geometries.append({'type':'Polygon','id':id,'arcs':f[0]})
        else:geometries.append({'type':'MultiPolygon','id':id,'arcs':f})
    return {'type':'Topology','transform':{'scale':[kx,ky],'translate':[x0,y0]},'objects':{'regions':{'type':'GeometryCollection','geometries':geometries}},'arcs':encoded}

def noded(db):
    """Regions rebuilt from one noded linework, so neighbours share every border vertex exactly.

    Clipping subdivisions to country outlines leaves vertices on one side of a border that the other side only passes
    through; snap-rounding all boundaries together inserts them on both sides before arcs are cut."""
    import numpy as np, shapely
    from shapely.geometry import shape, mapping
    rows=db.execute('SELECT id,geometry FROM regions ORDER BY rowid').fetchall()
    full=np.array([shape(json.loads(g)) for _,g in rows])
    lines=shapely.union_all(shapely.boundary(full),grid_size=NODING_GRID)
    faces=shapely.get_parts(shapely.polygonize(shapely.get_parts(lines)))
    # Each face lies inside exactly one region; faces inside none (enclosed lakes) are not territory.
    region,face=shapely.STRtree(shapely.point_on_surface(faces)).query(full,predicate='contains')
    parts=[[] for _ in rows]
    for r,f in zip(region,face):parts[r].append(faces[f])
    out=[]
    for (id,_),g,own in zip(rows,full,parts):
        rebuilt=shapely.union_all(own,grid_size=NODING_GRID) if own else g
        if rebuilt.geom_type not in ('Polygon','MultiPolygon'):rebuilt=g
        # Residual territories are mostly slivers where subdivision coastlines miss the country outline. Parts too
        # thin or small to see at display tolerance only fragment every neighbouring arc; the largest part stays.
        pieces=shapely.get_parts(rebuilt);area=shapely.area(pieces);width=2*area/np.maximum(shapely.length(pieces),1e-12)
        keep=(area>=REGION_TOLERANCE**2/4)&(width>=REGION_TOLERANCE/2);keep[area.argmax()]=True
        out.append((id,mapping(shapely.multipolygons(pieces[keep]) if keep.sum()>1 else pieces[keep][0])))
    return out

def regions(db):
    return json.dumps(topology(noded(db),REGION_TOLERANCE),separators=(',',':')).encode()

def world():
    from shapely import set_precision
    from shapely.geometry import shape, mapping
    from import_data import normalize
    from import_regions import polygons
    features=[]
    for n,f in enumerate(json.loads((ROOT/'data/source/countries.geojson').read_text())['features']):
        f=normalize(f,n);full=shape(f['geometry'])
        g=set_precision(polygons(full.simplify(WORLD_TOLERANCE,preserve_topology=True)),1e-4)
        # Islands smaller than the tolerance keep their unsimplified outline rather than vanishing.
        if g.is_empty:g=set_precision(polygons(full),1e-4)
        if g.is_empty:g=full
        features.append({'type':'Feature','id':f['id'],'properties':f['properties'],'geometry':mapping(g)})
    return json.dumps({'type':'FeatureCollection','features':features},separators=(',',':')).encode()

def decode(topo):
    """Region features from a quantized, delta-encoded topology (used by tests)."""
    (sx,sy),(tx,ty)=topo['transform']['scale'],topo['transform']['translate'];arcs=[]
    for arc in topo['arcs']:
        x=y=0;points=[]
        for dx,dy in arc:x+=dx;y+=dy;points.append((x*sx+tx,y*sy+ty))
        arcs.append(points)
    def ring(indices):
        points=[]
        for i in indices:
            a=arcs[i] if i>=0 else arcs[~i][::-1]
            points.extend(a if not points else a[1:])
        return points
    features=[]
    for g in topo['objects']['regions']['geometries']:
        if g['type']=='Polygon':coordinates=[ring(r) for r in g['arcs']]
        elif g['type']=='MultiPolygon':coordinates=[[ring(r) for r in p] for p in g['arcs']]
        else:coordinates=[]
        features.append({'type':'Feature','id':g.get('id'),'geometry':{'type':g['type'],'coordinates':coordinates}})
    return features

def main():
    db=sqlite3.connect(ROOT/'data/world.sqlite');manifest_path=ROOT/'data/source/gbOpen/manifest.json'
    manifest=json.loads(manifest_path.read_text())
    manifest['geometry_sha256']=geometry_digest(db)
    manifest['display_sha256']=write(ROOT/'web/regions.topojson',regions(db))
    manifest['world_display_sha256']=write(ROOT/'web/world.geojson',world())
    manifest['display_tolerance_degrees']={'regions':REGION_TOLERANCE,'world':WORLD_TOLERANCE}
    manifest_path.write_text(json.dumps(manifest,indent=2))
    print(f"Display geometry: regions.topojson {(ROOT/'web/regions.topojson').stat().st_size:,} bytes, world.geojson {(ROOT/'web/world.geojson').stat().st_size:,} bytes",flush=True)
if __name__=='__main__':main()
