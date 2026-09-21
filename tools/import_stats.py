#!/usr/bin/env python3
"""Import national statistics, the regional population/GDP split and the January 2026 world seed.

Runs after import_regions.py because regional shares need regions and located cities. Offline by default;
--download explicitly refreshes the pinned World Bank snapshot in data/source/country_stats.json.
"""
import argparse, datetime, json, pathlib, sqlite3, urllib.request
ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT/'data/source'
INDICATORS = {'SP.POP.TOTL': 'population', 'NY.GDP.MKTP.CD': 'gdp_usd', 'NY.GDP.MKTP.KD.ZG': 'gdp_growth',
              'SP.POP.GROW': 'pop_growth', 'GOV_WGI_PV.SC': 'stability', 'PA.NUS.FCRF': 'usd_rate',
              'MS.MIL.TOTL.P1': 'military_personnel', 'MS.MIL.XPND.CD': 'military_spend_usd'}
API = 'https://api.worldbank.org/v2'
EURO_AREA = 'XC'
# Regional split: part follows verified city population, part is spread evenly as a rural floor.
URBAN_WEIGHT = 0.7

def fetch(path):
    with urllib.request.urlopen(API + path, timeout=60) as r:
        return json.loads(r.read())

def download():
    countries = fetch('/country?format=json&per_page=400')[1]
    by_name = {c['name']: c['iso2Code'] for c in countries}
    previous = json.loads((SOURCE / 'country_stats.json').read_text()) if (SOURCE / 'country_stats.json').exists() else {}
    snapshot = {'source': 'World Bank Open Data API (World Development Indicators; Worldwide Governance Indicators)',
                'license': 'CC BY 4.0', 'retrieved': datetime.date.today().isoformat(), 'indicators': {}}
    # The military snapshot is dated separately and is not produced here; keep it across refreshes.
    if 'military_retrieved' in previous:
        snapshot['military_retrieved'] = previous['military_retrieved']
    for indicator in INDICATORS:
        source = '&source=3' if indicator.startswith('GOV_WGI') else ''
        rows = fetch(f'/country/all/indicator/{indicator}?format=json&mrnev=1&per_page=400{source}')[1]
        values = {}
        for row in rows:
            # Use the API's own two-letter id, falling back to the country name. countryiso3code
            # is never trusted: the API ships non-standard entities carrying another country's
            # iso3 -- "Taiwan, China" arrives as SYR and "Netherlands Antilles" as NLD -- and
            # resolving those silently overwrote Syria and the Netherlands. A row with no id and
            # an unrecognised name is an entity we do not model, so dropping it is correct.
            code = row['country']['id'] or by_name.get(row['country']['value'])
            if code and row['value'] is not None:
                values[code] = {'value': row['value'], 'year': int(row['date'])}
        snapshot['indicators'][indicator] = dict(sorted(values.items()))
    (SOURCE/'country_stats.json').write_text(json.dumps(snapshot, indent=0, sort_keys=True) + '\n')

def national(codes):
    snapshot = json.loads((SOURCE/'country_stats.json').read_text())
    currencies = json.loads((SOURCE/'currencies.json').read_text())['countries']
    overrides = json.loads((SOURCE/'stats_overrides.json').read_text())['countries']
    wb = snapshot['indicators']
    rates = wb['PA.NUS.FCRF']
    stats = {}
    for code in codes:
        o = overrides.get(code, {})
        s = {'currency': currencies[code]['code'], 'symbol': currencies[code]['symbol'], 'estimated': 0, 'year': 0,
             'source': 'World Bank Open Data (CC BY 4.0), retrieved ' + snapshot['retrieved']}
        for indicator, field in INDICATORS.items():
            if field in ('usd_rate', 'military_personnel', 'military_spend_usd'):
                continue
            if field in o:
                s[field] = o[field]; s['estimated'] = 1
            elif code in wb[indicator]:
                s[field] = wb[indicator][code]['value']
                if field == 'gdp_usd': s['year'] = wb[indicator][code]['year']
            else:
                raise SystemExit(f'{code}: missing {field}; add an estimate to stats_overrides.json')
        if 'usd_rate' in o: s['usd_rate'] = o['usd_rate']; s['estimated'] = 1
        elif 'usd_rate_like' in o: s['usd_rate'] = rates[o['usd_rate_like']]['value'] if o['usd_rate_like'] in rates else 1.0
        elif s['currency'] == 'USD': s['usd_rate'] = 1.0
        elif s['currency'] == 'EUR': s['usd_rate'] = rates[EURO_AREA]['value']
        elif code in rates: s['usd_rate'] = rates[code]['value']
        else:
            peers = [c for c in sorted(currencies) if currencies[c]['code'] == s['currency'] and c in rates]
            if not peers: raise SystemExit(f'{code}: no exchange rate for {s["currency"]}')
            s['usd_rate'] = rates[peers[0]]['value']
        if o.get('source'): s['source'] = o['source'] if s['estimated'] else s['source'] + '; ' + o['source']
        if s['year'] == 0: s['year'] = 2025
        s['population'] = int(round(s['population'])); s['gdp_usd'] = int(round(s['gdp_usd']))
        stats[code] = s
    return stats

def split(total, weights):
    """Largest-remainder integer allocation that sums exactly to total."""
    whole = sum(weights.values())
    if total == 0 or whole == 0:
        return {k: 0 for k in weights}
    raw = {k: total * w / whole for k, w in weights.items()}
    out = {k: int(v) for k, v in raw.items()}
    for k in sorted(raw, key=lambda k: (-(raw[k] - out[k]), k))[:total - sum(out.values())]:
        out[k] += 1
    return out

def regional(db, stats):
    rows = db.execute("SELECT r.id,r.affiliation,r.kind,COALESCE(SUM(c.population),0) FROM regions r LEFT JOIN region_cities c ON c.region=r.id WHERE r.affiliation!='' GROUP BY r.id ORDER BY r.id").fetchall()
    by_actor = {}
    for rid, code, kind, urban in rows:
        by_actor.setdefault(code, []).append((rid, kind, urban or 0))
    out = {}
    for code, regions in by_actor.items():
        eligible = [r for r in regions if r[1] != 'residual'] or regions
        urban = sum(r[2] for r in eligible)
        weights = {r[0]: URBAN_WEIGHT * (r[2] / urban if urban else 1 / len(eligible)) + (1 - URBAN_WEIGHT) / len(eligible) for r in eligible}
        population = split(stats[code]['population'], weights)
        gdp = split(stats[code]['gdp_usd'], weights)
        for rid, *_ in regions:
            out[rid] = (population.get(rid, 0), gdp.get(rid, 0))
    return out

def month_offset(text):
    year, month = map(int, text.split('-'))
    return (year - 2026) * 12 + month - 1

def seed(db, codes):
    world = json.loads((SOURCE/'world_2026.json').read_text())
    names = {}
    for rid, code, name in db.execute("SELECT id,affiliation,name FROM regions WHERE kind!='residual'"):
        names.setdefault((code, name), []).append(rid)
    def region(pair):
        ids = names.get(tuple(pair), [])
        if len(ids) != 1: raise SystemExit(f'world_2026.json: region {pair} resolves to {ids}')
        return ids[0]
    actors = set(codes) | set(world['actors'])
    for a, v in world['actors'].items():
        if v['base'] not in codes: raise SystemExit(f'world_2026.json: unknown base {v["base"]}')
    document = {'actors': world['actors'], 'conflicts': [], 'commitments': []}
    for kind in ('conflicts', 'commitments'):
        for item in world[kind]:
            unknown = [a for a in item['actors'] if a not in actors]
            if unknown: raise SystemExit(f'world_2026.json: {item["name"]} has unknown actors {unknown}')
            resolved = {k: item[k] for k in ('name', 'kind', 'actors', 'text')}
            resolved['started_turn'] = month_offset(item['started'])
            if kind == 'conflicts':
                resolved['intensity'] = item['intensity']
                resolved['occupied'] = {a: [region(p) for p in pairs] for a, pairs in item['occupied'].items()}
                resolved['affected_regions'] = [region(p) for p in item['affected']]
                resolved['entities'] = [dict(e, region=region(e['region'])) for e in item.get('entities', [])]
            document[kind].append(resolved)
    return document

def military(codes, stats, world):
    snapshot = json.loads((SOURCE/'country_stats.json').read_text())
    curated = json.loads((SOURCE/'military_2026.json').read_text())['countries']
    overrides = json.loads((SOURCE/'stats_overrides.json').read_text()).get('military', {})
    result = {}
    for code in sorted(set(codes) | set(world['actors'])):
        c, o = curated.get(code, {}), overrides.get(code, {})
        personnel = snapshot['indicators'].get('MS.MIL.TOTL.P1', {}).get(code, {})
        spend = snapshot['indicators'].get('MS.MIL.XPND.CD', {}).get(code, {})
        missing = []
        # Curated active-duty counts outrank MS.MIL.TOTL.P1, which counts active plus paramilitary
        # forces and would report state police and gendarmeries as soldiers. See docs/DATA.md.
        if 'active' in o:
            active, active_year = o['active'], o.get('year', 0)
        elif 'active' in c:
            active, active_year = c['active'], c.get('active_year', c.get('year', 0))
        else:
            active, active_year = personnel.get('value'), personnel.get('year', 0)
        if active is None:
            active = max(200, int(stats[code]['population'] * 0.003 + 0.5)) if code in stats else 200
            active_year = 0
            missing.append('active=0.3% population (minimum 200)')
        # Spending keeps its World Bank observation where there is one; curation only fills gaps.
        if spend:
            spend_usd, spend_year = spend.get('value', 0), spend.get('year', 0)
        else:
            spend_usd = c.get('spend_usd', 0)
            spend_year = c.get('spend_year', c.get('year', 0)) if 'spend_usd' in c else 0
        s = {'active': int(active), 'spend_usd': int(spend_usd),
             'active_year': active_year, 'spend_year': spend_year,
             'year': c.get('year', personnel.get('year', 0)), 'estimated': int(bool(o) or bool(c.get('estimated'))),
             'source': 'World Bank MS.MIL.TOTL.P1/MS.MIL.XPND.CD (CC BY 4.0); military snapshot retrieved ' + snapshot.get('military_retrieved', snapshot['retrieved'])}
        for field, fallback in [('reserve', s['active'] // 2), ('ships', 0), ('aircraft', 0)]:
            s[field] = c.get(field, fallback)
            if field not in c: missing.append(field + '=fallback')
        if not spend and 'spend_usd' not in c: missing.append('spend_usd=unknown (0)')
        if c.get('source'): s['source'] += '; ' + c['source']
        if o.get('source'): s['source'] += '; ' + o['source']
        if code in world['actors']:
            actor = world['actors'][code]
            for field in ('active', 'reserve', 'ships', 'aircraft'): s[field] = actor.get(field, 0)
            s.update(year=2025, active_year=2025, estimated=1, source='world_2026.json: plan 19 non-state actor estimate; spending unknown')
        elif missing:
            s['estimated'] = 1
            s['source'] += '; stats_overrides.json military_fallbacks: ' + ', '.join(missing)
        for field in ('active', 'reserve', 'ships', 'aircraft', 'spend_usd'):
            if not isinstance(s[field], int) or s[field] < 0: raise SystemExit(f'{code}: invalid military {field}')
        result[code] = s
    return result

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--download', action='store_true'); args = parser.parse_args()
    if args.download:
        download()
    db = sqlite3.connect(ROOT/'data/world.sqlite')
    codes = [c for c, in db.execute('SELECT code FROM countries ORDER BY code')]
    stats = national(codes)
    world = seed(db, codes)
    inventories = military(codes, stats, world)
    db.executescript('DROP TABLE IF EXISTS country_stats; CREATE TABLE country_stats(code TEXT PRIMARY KEY,population INTEGER,gdp_usd INTEGER,gdp_growth REAL,pop_growth REAL,stability REAL,currency TEXT,symbol TEXT,usd_rate REAL,year INTEGER,estimated INTEGER,source TEXT);'
                     'DROP TABLE IF EXISTS region_stats; CREATE TABLE region_stats(id TEXT PRIMARY KEY,population INTEGER,gdp_usd INTEGER);'
                     'DROP TABLE IF EXISTS world_seed; CREATE TABLE world_seed(id INTEGER PRIMARY KEY CHECK(id=1),document TEXT NOT NULL);')
    db.executescript('DROP TABLE IF EXISTS military_stats; CREATE TABLE military_stats(code TEXT PRIMARY KEY,active INTEGER,reserve INTEGER,ships INTEGER,aircraft INTEGER,spend_usd INTEGER,year INTEGER,estimated INTEGER,source TEXT,active_year INTEGER,spend_year INTEGER);')
    db.executemany('INSERT INTO military_stats VALUES(?,?,?,?,?,?,?,?,?,?,?)', [(c, *[s[k] for k in ('active','reserve','ships','aircraft','spend_usd','year','estimated','source','active_year','spend_year')]) for c,s in inventories.items()])
    db.executemany('INSERT INTO country_stats VALUES(?,?,?,?,?,?,?,?,?,?,?,?)',
                   [(c, s['population'], s['gdp_usd'], s['gdp_growth'], s['pop_growth'], s['stability'], s['currency'], s['symbol'], s['usd_rate'], s['year'], s['estimated'], s['source']) for c, s in stats.items()])
    regions = regional(db, stats)
    db.executemany('INSERT INTO region_stats VALUES(?,?,?)', [(r, p, g) for r, (p, g) in sorted(regions.items())])
    db.execute('INSERT INTO world_seed VALUES(1,?)', (json.dumps(world, sort_keys=True),))
    db.commit()
    print(f'Imported statistics for {len(stats)} actors ({sum(s["estimated"] for s in stats.values())} with estimates), {len(regions)} regional shares and the January 2026 world seed')

if __name__ == '__main__':
    main()
