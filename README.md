# LocalHistoria

A local C++20/Drogon strategy sandbox in the spirit of Pax Historia. Choose one of 239 nations, begin in **January 2026**, and each month write what your nation does in your own words. A local model decides what actually happens, what other nations do in response, and how the map changes: occupations, annexations, new states, construction, treaties and wars. There are no fixed action types, costs or statistics. The world's state is its map, its entities and its history.

## Run

A reachable **llama-server** is required; there is no offline/scripted mode. See [model setup](docs/MODELS.md) to start one on port 8081 before continuing.

On macOS with Homebrew:

```sh
brew install cmake drogon jsoncpp sqlite
cd LocalHistoria
make
make test
./build/localhistoria --llama-endpoint=http://127.0.0.1:8081
```

Open **http://127.0.0.1:8080** (use this address, not `localhost`). Run from the project root. CMake discovers installed dependencies. On Linux, install Drogon, JsonCpp, SQLite development packages, a C++20 compiler, CMake and Python 3, then use the same commands. Set `CMAKE_PREFIX_PATH` when configuring manually for a custom dependency prefix.

The `--llama-endpoint=` flag is optional; it defaults to `http://127.0.0.1:8081`. You only need to give it once: the server saves the active endpoint (from the flag or the web UI's **Model endpoint** field) in the campaign save and reuses it on the next launch.

The supplied map, compressed city dataset, all 4:3 flags, and Leaflet are project-owned assets. The build imports the data on demand. It never reads Downloads or downloads models. Rebuild reference data explicitly (see [Evolving world](docs/EVOLVING_WORLD.md)) while the server is stopped.

## Play

1. Select a country using the map or searchable list, then choose **Play as this country**.
2. Write up to eight actions for the month, e.g. *"Build a naval base in Faro"*, *"Propose a defensive alliance to Spain"* or *"Launch a national education reform"*. While an action is selected, click map regions to pin them as targets. This is optional, since regions and nations named in the text are recognized too.
3. Click **End month**. The model writes the actions of the nations you addressed, your ongoing rivals and a neighbor. It then adjudicates everything together and narrates the month. Drafts survive a page reload.
4. The game autosaves each completed turn. Reload or restart to resume. A new campaign requires confirmation before replacing the active campaign and journal.

A turn is one month, and the game keeps outcomes realistic:

- Force reaches only regions bordering land you control, or near your active armies and fleets. At most two regions can change hands by force per month.
- Conquest takes two steps: occupy a region first, then annex it in a later month.
- Armies advance one region per month and fleets sail up to 1,500 km.
- Construction takes months: armies 2, bases 3, fleets 4, ports 6, cities 24.
- At most one new state can emerge per month, and it needs territory.
- An action beyond these limits still appears in the journal as a failed attempt, and the rest of the month proceeds.

Actions without map effects (reforms, trade, research, diplomacy) are recorded as events and shape later adjudication through the campaign history.

If the model is unreachable, times out, or returns an illegal or malformed result, the whole turn fails with no state change. Resubmit once the model is healthy. Narration is non-authoritative and may lag the saved turn; reload to see it. If narration fails, a factual event summary is kept. Inference is serialized. Adjudication reasons briefly before answering, so a month takes roughly one to two minutes on a 4B model.

## Optional Wikipedia reference

Open a country's **Cities & reference sources** section and explicitly choose **Download / refresh Wikipedia reference**. This is the only gameplay feature that contacts external services. It resolves a country entity by ISO code (with four explicit overrides), follows its English Wikipedia sitelink, and caches a bounded excerpt with attribution, article URL, revision, language and retrieval time. Missing/ambiguous mappings do not block play. A failed refresh preserves an existing valid cache. Cached references are copied into each turn's replay record; later refreshes do not rewrite past turns.

Current articles may contain events after January 2026. This is a fictional sandbox, not a historically constrained simulation. The supplied geography's exact upstream revision and license were not included; see [data provenance](docs/DATA.md) before redistributing it.

## Verification

```sh
make test                       # C++ rule/provider/store tests + data integrity
npm ci                          # browser tooling only; not a game runtime dependency
npx playwright install chromium # one-time test browser installation
python3 tests/integration.py     # real loopback HTTP, SQLite, mock model, browser
```

Integration tests create disposable temporary saves and never replace your active campaign. They exercise hard failure on a down/slow/malformed model through a real HTTP client (no state change, clear error), duplicate/overlapping requests, restart and interrupted-job recovery, the CLI flag and web-UI endpoint setting persisting across restarts, free-text action cards with map targets, journal display and browser reload. The browser blocks all external requests. Test screenshots go to ignored `tests/artifacts/`. These mocks do not establish actual model quality or throughput. See [verification evidence](docs/VERIFICATION.md).

## Project guide

- [Architecture and extension points](docs/ARCHITECTURE.md)
- Replay verification: `./build/replay_verify campaign-export.json` checks every saved state and event without inference.
- [API contract](docs/API.md)
- [Data provenance and third-party notices](docs/DATA.md)
- `include/localhistoria/`, `src/`: simulation, persistence, replaceable providers, history, HTTP/job orchestration
- `web/`: plain HTML/CSS/JavaScript, local Leaflet, flags, generated GeoJSON
- `data/source/`: unchanged supplied inputs; `data/world.sqlite`: reproducible reference database
- `saves/`: ignored campaign files; `config/`: example `port`/`save_path`/`timeout_seconds` config file

Single-user, loopback-only scaffold. Fine-tuning, bundled models, multiplayer and production packaging are outside its scope.
