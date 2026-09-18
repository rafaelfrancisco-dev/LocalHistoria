# Data provenance and licenses

The imported database and normalized map are reproducible from project-owned inputs. `data/checksums.json` records SHA-256 digests. The importer does not use external services or machine-specific paths. The database contains 239 country metadata records and 152,970 supplied city records. There are 258 unique map features: 239 with normalized actor codes and 19 unresolved features. National statistics come from a pinned World Bank snapshot and documented estimates (below); they are never derived from city records.

| Asset | Provenance / terms |
|---|---|
| `data/source/countries.geojson` | Supplied by the user. Properties contain names and alpha-2/alpha-3 codes, without source revision or license. Exact upstream origin remains unverified. Geography resembles Natural Earth-derived data, but that resemblance does not establish provenance. |
| `data/source/json-cities.json.gz` | Supplied by the user. Schema matches [dr5hn/countries-states-cities-database](https://github.com/dr5hn/countries-states-cities-database). Exact snapshot revision unknown. Upstream ODbL text retained in `licenses/cities-LICENSE`. |
| `web/flags/*.svg` | Copied from the supplied flag-icons archive, only `flags/4x3`. MIT license retained verbatim. Exact supplied archive commit unknown. |
| `data/source/country_stats.json` | World Bank Open Data API snapshot (population, GDP in current US$, real GDP growth, population growth, official exchange rate, and the Worldwide Governance Indicators political stability score, 0–100). Latest non-null value per indicator, with its year. CC BY 4.0; see `licenses/WorldBank-NOTICE.md`. Refresh explicitly with `python3 tools/import_stats.py --download`. |
| `data/source/currencies.json` | Hand-authored ISO 3166 → ISO 4217 map with display symbols, as of January 2026 (Bulgaria uses the euro). |
| `data/source/stats_overrides.json` | Documented estimates, each with a source note, for the 33 actors the World Bank omits or leaves incomplete (e.g. Taiwan, North Korean GDP, small dependencies, uninhabited territories). They are flagged `estimated` in the game. |
| `data/source/world_2026.json` | Curated wars, occupations and treaties in force on 1 January 2026, with front regions and three non-state parties (RSF, Myanmar's National Unity Government, AFC/M23). Simplified to whole ADM1 regions; a game starting point, not an authoritative record. |
| `web/vendor/leaflet.js`, `.css` | Leaflet 1.9.4, downloaded from version-pinned unpkg URLs. BSD-2-Clause license retained. No tile provider is used. |
| `web/vendor/topojson-client.min.js` | topojson-client 3.1.0, `dist/topojson-client.min.js` from the npm tarball. ISC license retained. |
| `web/regions.topojson`, `web/world.geojson` (+ `.gz`) | Derived display geometry, written by `tools/display_geometry.py` from `data/world.sqlite` and `countries.geojson`. Simplified for the browser only; see [Geography](EVOLVING_WORLD.md#geography). Checksums `display_sha256` / `world_display_sha256` in `data/source/gbOpen/manifest.json`. |
| Drogon 1.9.13 | Installed library, MIT license retained. Transitive dependencies remain separately licensed in their installed distributions. |
| JsonCpp 1.9.8 | Installed library, upstream MIT/public-domain notice retained. |
| SQLite | Linked system/development library; [public domain](https://sqlite.org/copyright.html). |
| Cached Wikipedia excerpts | Wikipedia contributors, CC BY-SA 4.0. Article/revision/retrieval/language metadata preserved alongside plain-text excerpts. Cache and saves are ignored. |
| Chronicles | Model-written summaries (at most 2,400 bytes) derived from cached Wikipedia history (`history_source`, the last 12,000 characters of the History section) and campaign events; Wikipedia contributors, CC BY-SA 4.0, for the derived material. Stored per nation in the save's `chronicles` table and cleared with a new campaign. A chronicle written before its nation's source was cached is rewritten from the source the first month it exists. The long source never enters turn records. |
| Country Wikidata identifiers | Resolved by P297 country code or explicit overrides, CC0. Separate from city identifiers. |

Confirm exact source provenance before redistributing the supplied geography or presenting the database as a verified upstream snapshot. This is not required for local play.

`tools/import_stats.py` runs after the region import and builds three tables:

- `country_stats`: one row per actor.
- `region_stats`: population and GDP per region. The split weights each region 70% by its share of the country's verified city population and 30% equally, with residual slivers excluded. Largest-remainder rounding makes regions sum exactly to national totals. It is an estimate: the game has no subnational census.
- `world_seed`: the 2026 world, with region names resolved to IDs. An unresolved name fails the import.

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
Ukraine 298,000). The fourteen plan-19 reference countries use its rounded scenario estimates
through `stats_overrides.json` and `military_2026.json`, with `estimated: 1`. The original World Bank
values remain intact. These overrides satisfy the scenario's reference bounds; they are not
claimed to be verified current figures. Aircraft observations from the 2025 directory remain
alongside the scenario value as `aircraft_observed_2025`, with page references in `aircraft_source`.
Notable differences: Portugal 21 observed / 27 scenario; China 1,583 / 1,900; Iran 218 / 330;
Israel 278 / 340. The plan's Israeli ship estimate also includes more vessels than the strict
frigate-plus-submarine definition supports. These scenario estimates must not be used as a
real-world military inventory report.

Missing fields use the explicit `military_fallbacks` policy in `stats_overrides.json`: active
`max(200, round(population × 0.003))`, reserves `floor(active × 0.5)`, ships/aircraft zero.
Missing spending is unknown, represented by zero. Every such row is flagged estimated and names
its missing fields. A documented zero is preserved rather than replaced by a fallback.
