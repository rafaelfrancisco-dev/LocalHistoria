# Data provenance and licenses

The imported database and normalized map are reproducible from project-owned inputs. `data/checksums.json` records SHA-256 digests. The importer does not use external services or machine-specific paths. The database contains 239 country metadata records and 152,970 supplied city records. There are 258 unique map features: 239 with normalized actor codes and 19 unresolved features. National statistics come from a pinned World Bank snapshot and documented estimates (below); they are never derived from city records.

| Asset | Provenance / terms |
|---|---|
| `data/source/countries.geojson` | Supplied by the user. Properties contain names and alpha-2/alpha-3 codes, without source revision or license. Exact upstream origin remains unverified. Geography resembles Natural Earth-derived data, but that resemblance does not establish provenance. |
| `data/source/json-cities.json.gz` | Supplied by the user. Schema matches [dr5hn/countries-states-cities-database](https://github.com/dr5hn/countries-states-cities-database). Exact snapshot revision unknown. Upstream ODbL text retained in `licenses/cities-LICENSE`. |
| `web/flags/*.svg` | Copied from the supplied flag-icons archive, only `flags/4x3`. MIT license retained verbatim. Exact supplied archive commit unknown. |
| `data/source/country_stats.json` | World Bank Open Data API snapshot (population, GDP in current US$, real GDP growth, population growth, official exchange rate, and the Worldwide Governance Indicators political stability score, 0–100). Latest non-null value per indicator, with its year. Rows are keyed by the API's own two-letter country id, falling back to the country name; `countryiso3code` is never trusted, because the API ships non-standard entities under another country's iso3 ("Taiwan, China" as `SYR`, "Netherlands Antilles" as `NLD`), which silently overwrote Syria and the Netherlands until plan 22. Rows with no id and an unrecognised name are entities the game does not model and are dropped. CC BY 4.0; see `licenses/WorldBank-NOTICE.md`. Refresh explicitly with `python3 tools/import_stats.py --download`. |
| `data/source/currencies.json` | Hand-authored ISO 3166 → ISO 4217 map with display symbols, as of January 2026 (Bulgaria uses the euro). |
| `data/source/stats_overrides.json` | Documented estimates, each with a source note, for the 33 actors the World Bank omits or leaves incomplete (e.g. Taiwan, North Korean GDP, small dependencies, uninhabited territories). They are flagged `estimated` in the game. |
| `data/source/world_2026.json` | Curated wars, occupations and treaties in force on 1 January 2026, with front regions and three non-state parties (RSF, Myanmar's National Unity Government, AFC/M23). Simplified to whole ADM1 regions; a game starting point, not an authoritative record. |
| `data/source/governments_2026.json` | Curated political system for every actor on 1 January 2026, one of twelve values, with the year the current constitutional order began where that is well established (0 otherwise). Classifications follow each state's constitution and the CIA World Factbook "Government type" field, which is US-government work in the public domain; the classification itself is project-owned. Rows that depart from the Factbook's formal label carry their own note saying why — Russia and Belarus are classed by the absence of competitive turnover rather than as semi-presidential and presidential republics, Cambodia by the CPP's monopoly rather than as a constitutional monarchy. A game starting point, not an authoritative constitutional classification, and no claim about sovereignty. |
| `data/source/military_2026.json` | Curated January 2026 inventories: active, reserve, ships, aircraft and gap-filling spending. Ship and some reserve counts are Wikipedia-derived (CC BY-SA 4.0); aircraft totals are factual figures from FlightGlobal *World Air Forces 2025*; active-duty strengths are plan 21 model estimates carrying no upstream licence. Every row is flagged `estimated` and names its own basis. See `licenses/military-data.md`. |
| `web/vendor/leaflet.js`, `.css` | Leaflet 1.9.4, downloaded from version-pinned unpkg URLs. BSD-2-Clause license retained. No tile provider is used. |
| `web/vendor/topojson-client.min.js` | topojson-client 3.1.0, `dist/topojson-client.min.js` from the npm tarball. ISC license retained. |
| `web/regions.topojson`, `web/world.geojson` (+ `.gz`) | Derived display geometry, written by `tools/display_geometry.py` from `data/world.sqlite` and `countries.geojson`. Simplified for the browser only; see [Geography](EVOLVING_WORLD.md#geography). Checksums `display_sha256` / `world_display_sha256` in `data/source/gbOpen/manifest.json`. |
| Drogon 1.9.13 | Installed library, MIT license retained. Transitive dependencies remain separately licensed in their installed distributions. |
| JsonCpp 1.9.8 | Installed library, upstream MIT/public-domain notice retained. |
| SQLite | Linked system/development library; [public domain](https://sqlite.org/copyright.html). |
| Cached Wikipedia excerpts | Wikipedia contributors, CC BY-SA 4.0. Article/revision/retrieval/language metadata preserved alongside plain-text excerpts. Cache and saves are ignored. |
| Chronicles | Model-written summaries (at most 2,400 bytes) derived from cached Wikipedia history (`history_source`, the last 12,000 characters of the History section) and campaign events; Wikipedia contributors, CC BY-SA 4.0, for the derived material. Stored per nation in the save's `chronicles` table and cleared with a new campaign. A chronicle written before its nation's source was cached is rewritten from the source the first month it exists. The long source never enters turn records. |
| Country Wikidata identifiers | Resolved by P297 country code or explicit overrides, CC0. Separate from city identifiers. |

`tools/import_governments.py` writes `governments_2026.json` from a per-system mapping of country
codes; the JSON is what the game reads, and the script exists so the classification stays reviewable.
Re-run it after editing the mapping, then `tools/import_stats.py`. Neither touches the network or the
geometry.

Confirm exact source provenance before redistributing the supplied geography or presenting the database as a verified upstream snapshot. This is not required for local play.

`tools/import_stats.py` runs after the region import and builds four tables:

- `country_stats`: one row per actor, including its political system and the year that system began.
- `region_stats`: population and GDP per region. The split weights each region 70% by its share of the country's verified city population and 30% equally, with residual slivers excluded. Largest-remainder rounding makes regions sum exactly to national totals. It is an estimate: the game has no subnational census.
- `world_seed`: the 2026 world, with region names resolved to IDs. An unresolved name fails the import.
- `military_stats`: one row per actor, plus the non-state parties. Observation years are kept separately from the curated scenario year.

`tools/sea_routes.py` writes the coastal-route graph as two tables of its own, from the region
geometry already imported and the land outline in `countries.geojson`; it never rebuilds geometry
and takes a few minutes (`make data/.sea-routes`).

- `sea_routes`: for each coastal region, every coastal region a fleet can reach within 1,500 km,
  with the route length in whole kilometres. Anchor points are placed along each region's own
  shoreline, one per degree cell, and linked by straight hops of at most 400 km that stay clear of
  land eroded by about 9 km. Erosion opens straits up to about 18 km wide (Gibraltar, the Bosporus
  and Dardanelles, the Oresund, Kerch) and isthmuses as narrow (Corinth, Perekop); the Suez,
  Panama and Kiel canals are added explicitly. A fleet in a region on two seas can sail from
  either, but no route crosses a region to reach the other sea. The graph is symmetric.
- `sea_route_meta`: one content revision. Each month records it in its references; a replay
  export carries the graph, and replay refuses a different one.

Exchange rates use the World Bank's latest official annual average: the euro-area rate for euro users, 1 for dollar users, a peer country's rate for shared currencies, and pegged currencies via `usd_rate_like`. Some rates are stale for high-inflation currencies.

Normalization is explicit: France→FR, Norway→NO, Kosovo→XK, Taiwan→TW. Other two-letter uppercase codes in the supplied map identify sandbox actors. `XK` is the explicit Kosovo sandbox convention; it does not assert ISO assignment or sovereignty. Unresolved territories are visible and non-playable. IDs `feature-000` etc. belong to features, never country codes, so unresolved codes cannot collide. Missing flags hide their image while preserving text; absent city records show an empty reference list.

`countries.country_wikidata` begins NULL; country mappings are stored with cached references after verified country-level lookup. `cities.city_wikidata` is only the city's supplied identifier. Country details never reuse it for article grounding. Country names come from the map, not summed city statistics.

Upstream API references: [MediaWiki REST API](https://www.mediawiki.org/wiki/API:REST_API/Reference), [Wikidata SPARQL](https://www.wikidata.org/wiki/Wikidata:SPARQL_query_service). The app only refreshes a selected country on explicit request, retains bounded excerpts rather than full articles, and records which source revision each turn used.

## Military inventories (January 2026 scenario)

`active` means active-duty people (the World Bank series also includes paramilitary personnel).
`reserve` means trained reserve personnel. `ships` means principal surface combatants of frigate
size or larger, including carriers and amphibious assault ships, plus submarines; patrol boats,
corvettes, mine warfare, landing craft and auxiliaries are excluded. `aircraft` means fixed-wing
fighters, attack aircraft and bombers, excluding trainers, helicopters, transports and drones.

`tools/import_stats.py` imports World Bank `MS.MIL.TOTL.P1` and `MS.MIL.XPND.CD`; `--download`
refreshes the snapshot. `military_stats` contains code, active, reserve, ships, aircraft, spend_usd,
year, estimated, source, active_year and spend_year. Observation years are independent: `year`
is the curated scenario year, not a claim that the World Bank observation is from that year.
The default import is offline. Non-state actors take their estimates from `world_2026.json`.

Sources: [Wikipedia personnel](https://en.wikipedia.org/wiki/List_of_countries_by_number_of_military_and_paramilitary_personnel),
[Wikipedia equipment](https://en.wikipedia.org/wiki/List_of_countries_by_level_of_military_equipment),
and [FlightGlobal World Air Forces 2025](https://www.flightglobal.com/download?ac=106507).
The two equipment-list titles proposed in plan 19 were unavailable; the actual equipment article
and the publisher's aircraft directory were used instead. Attribution and licences are in
[licenses/military-data.md](../licenses/military-data.md). Curated entries carry source notes;
ship estimates based on mixed-date tables are provisional rather than verified 2025 observations.
Amphibious categories are excluded where assault ships cannot be separated from landing craft.

**Source discrepancies are explicit.** The newest downloaded World Bank personnel observations
are generally from 2020 and include paramilitary forces (Portugal 52,000; India 3,068,000;
Ukraine 298,000). Plan 19 replaced that series for fourteen reference countries; plan 21 replaced
it for every remaining actor, because counting gendarmeries and state police as soldiers inflated
many armies by roughly half (Brazil read 762,000 against a standing army near 360,000, France
304,000 against 205,000). Every `active` value now comes from `military_2026.json` with
`estimated: 1`; `stats_overrides.json` remains the higher-priority patch layer and the World Bank
series the last resort. The original World Bank values remain intact. These overrides satisfy the scenario's reference bounds; they are not
claimed to be verified current figures. Aircraft observations from the 2025 directory remain
alongside the scenario value as `aircraft_observed_2025`, with page references in `aircraft_source`.
Notable differences: Portugal 21 observed / 27 scenario; China 1,583 / 1,900; Iran 218 / 330;
Israel 278 / 340. The plan's Israeli ship estimate also includes more vessels than the strict
frigate-plus-submarine definition supports. These scenario estimates must not be used as a
real-world military inventory report.

The plan-21 active-duty strengths are rounded model estimates for January 2026, not cited
observations, and every entry says so in its `source`. They are the weakest provenance in the
database and should be replaced with cited figures when a usable source is available. States with
no armed forces are recorded as an explicit zero rather than a population-derived garrison: Costa
Rica and Panama abolished theirs, Iceland never had one, and dependent territories are defended by
another state. Military spending keeps its World Bank observation wherever there is one; curation
only fills gaps.

Missing fields use the explicit `military_fallbacks` policy in `stats_overrides.json`: active
`max(200, round(population × 0.003))`, reserves `floor(active × 0.5)`, ships/aircraft zero.
Missing spending is unknown, represented by zero. Every such row is flagged estimated and names
its missing fields. A documented zero is preserved rather than replaced by a fallback. No shipped
row relies on the active or reserve fallback any more; the policy remains for actors added later.
