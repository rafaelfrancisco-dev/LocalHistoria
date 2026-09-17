import json,pathlib,sqlite3
from shapely.geometry import shape
root=pathlib.Path(__file__).resolve().parents[1]
db=sqlite3.connect(root/'data/world.sqlite')
db.execute('CREATE TABLE IF NOT EXISTS region_interiors(id TEXT PRIMARY KEY,longitude REAL,latitude REAL)')
for id,g in db.execute('SELECT id,geometry FROM regions').fetchall():
 p=shape(json.loads(g)).representative_point();db.execute('INSERT OR REPLACE INTO region_interiors VALUES(?,?,?)',(id,p.x,p.y))
db.commit()
