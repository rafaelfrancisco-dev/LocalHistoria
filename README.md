> Campaign geography, adjudication, historical preparation and current verification are described in [Evolving world](docs/EVOLVING_WORLD.md). This supersedes the original country-statistics-only behavior described below.

# LocalHistoria

A playable, local C++20/Drogon strategy sandbox. Choose one of 239 map actors, begin in **January 2026**, issue monthly orders, and build a fictional diplomatic history. All national statistics are deliberately invented and equal at the start. Country codes identify sandbox actors without asserting sovereignty. Combat and conquest are not implemented.

## Run

A reachable **llama-server** is required — there is no offline/scripted mode. See [model setup](docs/MODELS.md) to install and start one on port 8081 before continuing.

On macOS with Homebrew:

```sh
brew install cmake drogon jsoncpp sqlite
cd LocalHistoria
make
make test
./build/localhistoria --llama-endpoint=http://127.0.0.1:8081
```

Open **http://127.0.0.1:8080** (use this address, not `localhost`). Run from the project root. CMake discovers installed dependencies; on Linux install Drogon, JsonCpp, SQLite development packages, a C++20 compiler, CMake and Python 3, then use the same commands. Set `CMAKE_PREFIX_PATH` when configuring manually for a custom dependency prefix.

The `--llama-endpoint=` flag is optional (it defaults to `http://127.0.0.1:8081`) and only needs to be given once — the server persists whatever endpoint is active (from the flag, or from the web UI's **Model endpoint** field) in the campaign save, and reuses it on the next launch with no flag needed.

The supplied map, compressed city dataset, all 4:3 flags, and Leaflet are project-owned assets. The build imports the data on demand; it never reads Downloads or downloads models. Rebuild reference data explicitly with `python3 tools/import_data.py` while the server is stopped.

## Play

1. Select a country using the map or searchable list, then choose **Play as this country**.
2. Add structured orders, or request a text proposal — the local model translates free text into a proposed order draft for you to review.
3. Edit and review the JSON draft, then click **Review complete — advance one month**. Proposals never submit themselves. All orders must validate together.
4. Pending alliances and active treaties appear above your order controls. Improve relations before an offer; each targeted actor's accept/reject decision is made by the model. New offers can only be answered next month.
5. The game autosaves each completed turn. Reload or restart to resume. A new campaign requires confirmation before replacing the active campaign and journal.

Each actor starts with treasury 100, monthly income 10, stability 60, army 10, navy 10, defensive posture, balanced priority and neutral relations. These are game units, not currency or historical measurements.

| Action | Cost | Effect |
|---|---:|---|
| Economy | 20 | Income +2 from the following month |
| Army / navy | 15 | Respective strength +3 |
| Improve relations | 5 | Bilateral relation +10, capped at 100 |
| Offer / accept / reject alliance | 0 | Pending offers and treaties |
| Posture | 0 | Defensive, neutral or assertive; descriptive until combat is added |
| Priority | 0 | Balanced, prosperity (+1 treasury/month), welfare (+1 stability/month) |

Affordability uses starting treasury; administration uses starting income and priorities. Up to eight orders per actor per turn; duplicate action/target combinations are rejected. Income/strength cap at 10,000, treasury at 1,000,000, stability at 100; campaigns stop after 12,000 months. The model decides only for directly targeted actors and actors with pending offers, with no per-turn cap on how many such decisions a turn needs. All countries still receive routine monthly administration.

Model decisions are schema constrained and validated in C++. If the model is unreachable, times out, or returns an oversized, illegal or malformed result, the whole turn (or proposal) fails outright with no state change — resubmit once the model is healthy. Narration is non-authoritative and may lag the saved turn; reload to see completed narration. Its failure preserves a factual event summary. Inference is serialized and defaults to a 20-second timeout per request.

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

Integration tests create disposable temporary saves and never replace your active campaign. They exercise hard failure on a down/slow/malformed model through a real HTTP client (no state change, clear error), duplicate/overlapping requests, restart and interrupted-job recovery, the CLI flag and web-UI endpoint setting persisting across restarts, reviewed proposals, journal display and browser reload. The browser blocks all external requests. Test screenshots go to ignored `tests/artifacts/`. These mocks do not establish actual model quality or throughput. See [verification evidence](docs/VERIFICATION.md).

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
