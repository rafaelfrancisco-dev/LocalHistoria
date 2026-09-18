# 08 · Map geometry: 124 MB GeoJSON → < 10 MB, gzip, caching, fewer refetches

**Status: done (2026-09-18).** Display geometry is `web/regions.topojson` (`tools/display_geometry.py`: noded linework → shared arcs → per-arc Douglas-Peucker 0.01°, residual slivers dropped), 8.5 MB / 3.0 MB gzipped. The Python `topojson` package was too slow on 4.5 M vertices, so arcs are built in-house. `world.geojson` is 3.1 MB / 0.9 MB. Served via `gzip_static` with an immutable, content-versioned URL. The layer is built once, `moveend` is debounced and cached by viewport and month, and `/campaign` has an ETag/304. Added a **Countries** map mode meshing national borders from current ownership (vendored topojson-client 3.1.0). Browser test: heap 153 MB (was 853), mode switch 3 ms synchronous restyle.

## Goal
The campaign page must not download 124 MB and hold 850 MB of heap.

## Evidence
- `web/regions.geojson`: 124,040,984 bytes, 3,421 features, 4.5 M vertices (2,347 Polygon, 1,074 MultiPolygon).
  Fetched by `web/app.js:561` (`refreshCampaignMap`) on every campaign page load; `web/world.geojson`
  (12.4 MB) by `web/app.js:959` on the pre-campaign screen.
- Measured in headless Chromium against the real save: JS heap 853 MB after load; switching map mode restyles
  all layers in 466 ms.
- Coordinates are on a 0.00000001° grid (`docs/EVOLVING_WORLD.md` Geography). Full-resolution geometry also
  lives in `data/world.sqlite` `regions.geometry` and is what `containsPoint` uses (`src/main.cpp:288`).
- Static files: Drogon `setDocumentRoot("web")`, `setFileTypes` at `src/main.cpp:817–819`.
- Map-move refetches: `web/app.js:557` (`moveend` → `refreshEntities`), `refreshEventMarkers` on load;
  `/api/v1/campaign` returns 1.1 MB.

## Steps
1. Display geometry. In `tools/import_regions.py` (which writes `web/regions.geojson`, see
   `docs/EVOLVING_WORLD.md` "Rebuild"), add a display pass: simplify each feature with shapely
   `simplify(tolerance, preserve_topology=True)`; pick tolerance (start 0.01°, try 0.02°) so the file is
   < 10 MB; round coordinates to 4 decimals; drop `properties` not used by `app.js` (it uses `id`, `name`,
   `code`/`owner`, `playable`). Write it as `web/regions.geojson` (display) and keep the full geometry only in
   SQLite. Do the same for `web/world.geojson` via `tools/import_data.py`. Shared borders may show hairline
   gaps at high zoom with per-feature simplification; if visible, simplify with topology preserved across
   features (e.g. `topojson` via the `topojson` Python package, or shapely `unary_union` of boundaries then
   re-polygonize). Keep the geometry checksum in `manifest.json` for the *source*; add a separate display
   checksum. Update `tests/regions_test.py` (it verifies coverage/overlap on the full data; point it at SQLite
   if it currently reads the GeoJSON) and `tests/data_test.py` (258 features count is for `world.geojson`).
2. Serve compressed and cached. Drogon: `app().setGzipStatic(true)` (or pre-generate `regions.geojson.gz`
   and let Drogon serve it, check `setStaticFilesCacheTime`). Add
   `setStaticFilesCacheTime(0)` for HTML/JS and a long `Cache-Control` for `*.geojson` (immutable content;
   version the URL: `/api/v1/map` returns `geometry_url`; make it `/regions.geojson?v=<checksum>`).
3. Client: `refreshCampaignMap` recreates the whole `L.geoJSON` layer on every `load()` (line 563–577).
   Instead, create it once and on later loads only update `feature.properties` and call `setStyle`.
4. Debounce `moveend` handler (300 ms) and skip `refreshEventMarkers`/`refreshEntities` when `state.turn`
   has not changed and bounds are within the last fetched bounds.
5. `/api/v1/campaign` (`src/main.cpp:446–452`): the client needs `state` for stats and `territories` for
   map properties; both are already there. Leave as is, but add `ETag: <turn>` and honour `If-None-Match`
   with 304 so polling during a turn is cheap (the `poll()` loop calls `/jobs` and `/inference`, not
   `/campaign`, so this is minor).
6. Docs: `docs/EVOLVING_WORLD.md` Geography (display vs. authoritative geometry), `docs/DATA.md`.

## Tests
- `tests/data_test.py`: assert `web/regions.geojson` < 12 MB and feature count unchanged (3,421 incl. residuals;
  read the count from SQLite to avoid hardcoding).
- `tests/browser.mjs`: after load, `performance.memory.usedJSHeapSize < 300 MB` (Chromium only; guard if
  undefined) and the Donetsk fill check still passes.
- `make geography-env && make data && make geography-test` runs clean.

## Acceptance
Page load transfers < 10 MB, heap < 300 MB, map mode switch < 150 ms, `containsPoint` still validates entities
against full geometry (`tests/world_tests.cpp` holes test unchanged).
