# 09 · UI cleanups

## Goal
Move developer settings out of the header, show what a month changed, fix the layer control, phone layout and
journal dates, and group the country list.

## Evidence (screenshots taken against the real save at 1440×1000 and 390×844)
- Header carries the "Model endpoint" input and Save button (`web/index.html:28–32`).
- "Prepare campaign references" is created in JS and prepended into the actions panel
  (`web/app.js:945–952`).
- Leaflet layer control is restyled via `::after` (`web/style.css:683–684`) and renders as a white box with
  dark text on the dark map.
- Journal: narration first, then a flat "Authoritative events" list where five distant war tolls
  (Russia, Sudan, Myanmar, Kivu, plus the player's) appear every month next to the player's outcomes
  (`web/app.js:290–318`). The live turn log is hidden on completion (`web/app.js:380`).
- Phone width: header wraps to three rows with the endpoint field squeezed; country list shows two entries;
  the actions panel is below the journal (`web/style.css:556–586`).
- Journal titles use `month(entry.turn - 1)` while the header shows the next month (`web/app.js:295`).
- Stats cells "conflicts · treaties 1 · 2" and "armies · fleets 0 · 0" (`web/app.js:194–200`).
- Country list: 242 flat entries including non-state actors (`web/app.js:65–88`).

## Steps
1. Settings dialog. Add `<dialog id="settings">` with the endpoint field + Save, a "Skip AI narration"
   checkbox (store in `localStorage`; when checked, the client passes nothing — narration is server-side, so
   add `narration: false` to the `/api/v1/turns` body, accept it in the field list at `src/main.cpp:378–386`,
   store it in the job payload and skip `provider.narrate` in the worker), a link to
   `/api/v1/inference?job=<last job>` and to `/api/v1/replay`. Header keeps a small "Settings" button next to
   "Events". Move "Prepare campaign references" into the HTML under "Cities & reference sources".
   Update `tests/integration.py` field validation (unknown field test at line 98 uses `action`, fine) and
   `tests/browser.mjs` if it touches the endpoint (it does not).
2. "What changed this month". In `load()` per journal entry, derive from `entry.structured_events`: regions
   that changed control (events with `regions` and an `occupy`/`transfer` — the structured event does not
   list operations; use `entry.events` `target` → `structured.id` and the change kinds are not exposed. Simplest
   server-side: in `Store::journal()` (`src/store.cpp:159–167`) add `month["changes"]` = the delta's `changes`
   reduced to `{kind,id,summary}` where summary is built in C++: territory → "<region>: control <from> → <to>"
   or "owner …"; entity → "<name> (<kind>) started/completed/destroyed"; conflict → "<name> opened/escalated/
   ended"; commitment → "<name> signed/ended". Read `reference` JSON `delta.changes` of the turn row.
   Render as a compact bullet list above the narration. Order the authoritative events: player's outcomes
   first (actor == player), then other nations, then a collapsed `<details>` "War tolls (n)".
3. Keep the turn log after completion inside the newest journal entry as `<details>` "How the month was
   decided" (`web/app.js:380` → move the lines instead of clearing).
4. Layer control: remove the `::after` hack; add "Entities" and "Events" toggle buttons to the existing
   `map-modes` bar (`web/app.js:592–606`), `aria-pressed`, toggling `map.addLayer/removeLayer` of
   `entityLayers`/`eventLayers`; remove `L.control.layers` (`web/app.js:542`).
5. Phone layout (`web/style.css` `@media (max-width: 600px)`): hide `#model-settings` (now in the dialog
   anyway), make the header a single row with the date and two icon buttons, turn the country list into the
   search box plus a dropdown of the first 8 matches, and reorder `main` with CSS `order` so `.panel`
   (stats + actions) comes before `.journal`. Test at 390 px in `tests/browser.mjs` with
   `page.setViewportSize` and assert `#turn` is within the first 2,000 px of the page.
6. Journal date: title "February 2026" plus a muted "played · now March 2026" using `month(entry.turn)`.
7. Stats: split the combined cells into four; add "last month" deltas from
   `state.actors[selected].status.last_population_change` and `last_gdp_change` under population and GDP.
8. Country list groups: "You", "Neighbors" (needs `/api/v1/countries` to return `neighbors` for the player:
   add a `neighbors` array in the route at `src/main.cpp:478–484` from the `neighbors` table), "At war or in
   treaty with you" (from `state.conflicts/commitments`), "Everyone". Hide actors without a static flag and
   without territory (landless non-state actors) from "Everyone" unless searched.
9. `#turn-log` has `aria-live="polite"`; set `aria-live="off"` and announce only phase changes (one line per
   model call kind) via a separate hidden live region.
10. Docs: README "Play" (settings, what changed, groups), `docs/API.md` (`narration` field, `neighbors`).

## Tests
`tests/browser.mjs`: settings dialog opens and saves the endpoint (assert `/api/v1/campaign` returns it);
"What changed" list contains the fixture port after the `transfer-and-build-fixture` turn; layer toggle hides
entity markers; phone viewport assertion from step 5. `python3 tests/integration.py` passes.

## Acceptance
All of the above pass; screenshots at 1440 and 390 px look right (save them to `tests/artifacts/`).
