#!/usr/bin/env python3
"""Import the checked-in geoBoundaries snapshot. --download explicitly accesses network.
Requires Shapely 2.0.7. Geometry is reconciled to supplied outlines, never vice versa.
"""
import argparse, concurrent.futures, hashlib, json, pathlib, sqlite3, urllib.request
from shapely.geometry import shape, mapping, Point, MultiPolygon
from shapely import make_valid, set_precision
from shapely.ops import unary_union
from import_data import normalize
ROOT=pathlib.Path(__file__).resolve().parents[1]
CACHE=ROOT/'data/source/gbOpen'
def polygons(g):
    g=make_valid(g)
    if g.geom_type=='Polygon': return g
    parts=[]
    for p in getattr(g,'geoms',[]):
        if p.geom_type=='Polygon': parts.append(p)
        elif p.geom_type in ('MultiPolygon','GeometryCollection'):
            q=polygons(p); parts.extend([q] if q.geom_type=='Polygon' else q.geoms)
    return MultiPolygon(parts)
def readable(name):
    # Some geoBoundaries names are UTF-8 bytes mis-decoded as Latin-1 (e.g. "RegiÃ³n"); repair them when that round-trips.
    try: return name.encode('latin-1').decode('utf-8')
    except (UnicodeEncodeError,UnicodeDecodeError): return name
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--download',action='store_true');a=parser.parse_args()
    CACHE.mkdir(parents=True,exist_ok=True)
    index=json.loads((CACHE/'index.json').read_text())
    if (CACHE/'manifest.json').exists() and not a.download:
        previous=json.loads((CACHE/'manifest.json').read_text())
        assert hashlib.sha256((CACHE/'index.json').read_bytes()).hexdigest()==previous['index_sha256'], 'Snapshot index changed'
        for m in previous['sources']:
            assert hashlib.sha256((CACHE/(m['boundaryISO']+'.json')).read_bytes()).hexdigest()==m['sha256'], 'Snapshot source changed'

    raw=json.loads((ROOT/'data/source/countries.geojson').read_text())['features']
    wanted={f['properties']['ISO3166-1-Alpha-3'] for f in raw}
    def download(m):
        path=CACHE/(m['boundaryISO']+'.json')
        if path.exists(): return
        url=m['simplifiedGeometryGeoJSON'].replace('https://github.com/','https://media.githubusercontent.com/media/').replace('/raw/','/')
        try:
            data=urllib.request.urlopen(url,timeout=60).read();json.loads(data);path.write_bytes(data)
        except Exception as e: print(m['boundaryISO'],str(e),flush=True)
    if a.download:
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:list(pool.map(download,[m for m in index if m['boundaryISO'] in wanted]))
    manifest={'index_sha256':hashlib.sha256((CACHE/'index.json').read_bytes()).hexdigest(),'sources':[],'countries':[]}
    features=[]; geos={}; affiliations={}; occupied=MultiPolygon([])
    byiso={m['boundaryISO']:m for m in index}
    for n,original in enumerate(raw):
        f=normalize(original,n); p=f['properties'];code=p['code'] or f['id']; iso=original['properties']['ISO3166-1-Alpha-3']
        outline=polygons(shape(f['geometry'])); overlap=outline.intersection(occupied).area
        # Supplied overlapping claims are explicit unresolved residuals, not duplicate territory.
        outline=polygons(outline.difference(occupied));occupied=unary_union([occupied,outline])
        path=CACHE/(iso+'.json'); used=MultiPolygon([]);count=0;rejected=[]
        def emit(id,name,g,kind):
            if g.is_empty or g.area==0:return
            prop={'id':id,'name':name,'code':p['code'],'initial_affiliation':p['code'] or '', 'kind':kind,'playable':bool(p['code'])}
            features.append({'type':'Feature','id':id,'properties':prop,'geometry':mapping(g)})
            geos[id]=g;affiliations[id]=p['code'] or ''
        if path.exists() and iso in byiso:
            manifest['sources'].append(dict(byiso[iso],sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
            for r in sorted(json.loads(path.read_text())['features'],key=lambda x:x['properties']['shapeID']):
                g=polygons(shape(r['geometry']).intersection(outline));rid='gb-'+r['properties']['shapeID']
                if g.intersection(used).area>1e-9:rejected.append(rid);continue
                emit(rid,readable(r['properties']['shapeName']),g,'ADM1');used=unary_union([used,g]);count+=not g.is_empty
        residual=polygons(outline.difference(used));emit(code+'-residual',p['name']+(' — uncovered territory' if count else ' — single territory'),residual,'residual' if count else 'single')
        manifest['countries'].append({'code':code,'regions':count,'missing_subdivisions':not path.exists(),'rejected_overlaps':rejected,'supplied_overlap_area':overlap,'residual_area':residual.area})
    # Final pass handles floating-point slivers after validity repair of supplied claims.
    from shapely.strtree import STRtree
    reconciled=[geos[f['id']] for f in features]; tree=STRtree(reconciled)
    manifest['final_overlap_reconciliation']=[]
    for i,g in enumerate(reconciled):
        for k in tree.query(g):
            if k<i and g.intersection(reconciled[k]).area>1e-10:
                manifest['final_overlap_reconciliation'].append((features[i]['id'],features[k]['id'],g.intersection(reconciled[k]).area))
                g=g.difference(reconciled[k])
        reconciled[i]=g;geos[features[i]['id']]=g;features[i]['geometry']=mapping(g)
    for f in features:
        g=set_precision(geos[f['id']],1e-8);geos[f['id']]=g;f['geometry']=mapping(g)
    manifest['coordinate_precision_degrees']=1e-8
    # Preserve any remaining coverage slivers as named residuals, including disputed outlines.
    shapes=[shape(f['geometry']) for f in features];coverage_tree=STRtree(shapes)
    for n,original in enumerate(raw):
        outline=set_precision(polygons(shape(original['geometry'])),1e-8)
        covered=unary_union([shapes[k] for k in coverage_tree.query(outline)])
        missing=outline.difference(covered)
        if missing.area>1e-7:
            f=normalize(original,n);code=f['properties']['code'];rid=(code or f['id'])+'-coverage-residual'
            features.append({'type':'Feature','id':rid,'properties':{'id':rid,'name':f['properties']['name']+' — unresolved coverage','code':code,'initial_affiliation':code or '', 'kind':'residual','playable':bool(code)},'geometry':mapping(missing)})
            geos[rid]=missing;affiliations[rid]=code or ''
    out={'type':'FeatureCollection','features':features}
    (ROOT/'web/regions.geojson').write_text(json.dumps(out,separators=(',',':')))
    manifest['geometry_sha256']=hashlib.sha256((ROOT/'web/regions.geojson').read_bytes()).hexdigest()
    (CACHE/'manifest.json').write_text(json.dumps(manifest,indent=2))
    db=sqlite3.connect(ROOT/'data/world.sqlite')
    db.executescript('DROP TABLE IF EXISTS regions; CREATE TABLE regions(id TEXT PRIMARY KEY,name TEXT,affiliation TEXT,kind TEXT,geometry TEXT); DROP TABLE IF EXISTS region_cities; CREATE TABLE region_cities(id TEXT PRIMARY KEY,region TEXT,name TEXT,latitude REAL,longitude REAL,population INTEGER); CREATE INDEX region_city_view ON region_cities(latitude,longitude,population); DROP TABLE IF EXISTS neighbors; CREATE TABLE neighbors(a TEXT,b TEXT,PRIMARY KEY(a,b));')
    db.executemany('INSERT INTO regions VALUES(?,?,?,?,?)',((f['id'],f['properties']['name'],f['properties']['initial_affiliation'],f['properties']['kind'],json.dumps(f['geometry'])) for f in features))
    from shapely.strtree import STRtree
    ids=list(geos); shapes=[geos[i] for i in ids];tree=STRtree(shapes)
    from shapely import prepare
    prepare(shapes)
    seeded=0
    for id,code,name,pop,lat,lon in db.execute('SELECT id,code,name,population,latitude,longitude FROM cities ORDER BY id').fetchall():
        pt=Point(lon,lat)
        for i in tree.query(pt):
            if affiliations[ids[i]]==code and shapes[i].covers(pt):
                db.execute('INSERT INTO region_cities VALUES(?,?,?,?,?,?)',('city-'+str(id),ids[i],name,lat,lon,pop));seeded+=1;break
    for i,g in enumerate(shapes):
        for k in tree.query(g):
            a,b=affiliations[ids[i]],affiliations[ids[k]]
            if a and b and a!=b and g.intersects(shapes[k]):db.execute('INSERT OR IGNORE INTO neighbors VALUES(?,?)',(a,b))
    db.commit();
    import region_interiors, region_neighbors
    print(f'Imported {len(features)} regions and {seeded} located cities',flush=True)
if __name__=='__main__':main()
