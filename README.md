# LocalHistoria

A local C++20/Drogon strategy sandbox in the spirit of Pax Historia. Choose one of 239 nations, begin in **January 2026**, and each month write what your nation does in your own words. A local model decides what actually happens, what other nations do in response, and how the map changes: occupations, annexations, new states, construction, treaties and wars. There are no fixed action types or costs. Each nation starts from its real January 2026 situation: population, GDP in its own currency (with a US dollar equivalent), political stability, and the wars and treaties it is part of. These evolve month by month and ground every model decision.

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

Or start the model and the game together with one command. It builds if needed, reuses a model server already running on port 8081, waits for the model to load and opens the browser. Ctrl+C stops both:

```sh
./start.sh                 # or: make start
./start.sh --no-open config/local-model.json
MODEL_FILE=~/models/Qwen3.5-4B-Q4_K_M.gguf ./start.sh
```

It needs llama.cpp (`llama` or `llama-server`) on your PATH. Logs go to `logs/`. `./start.sh --help` lists the overrides (`MODEL`, `MODEL_FILE`, `MODEL_PORT`, `CTX_SIZE`).

Open **http://127.0.0.1:8080** (use this address, not `localhost`). Run from the project root. CMake discovers installed dependencies. On Linux, install Drogon, JsonCpp, SQLite development packages, a C++20 compiler, CMake and Python 3, then use the same commands. Set `CMAKE_PREFIX_PATH` when configuring manually for a custom dependency prefix.

### Windows

Install [Visual Studio](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload (it bundles CMake and vcpkg) and Python 3. From a regular PowerShell prompt (no need for a "Developer" shell — `start.ps1` locates CMake and vcpkg itself):

```powershell
git clone <repo> LocalHistoria
cd LocalHistoria
$env:VCPKG_ROOT = "$env:ProgramFiles\Microsoft Visual Studio\18\Community\VC\vcpkg"  # adjust the "18" for your VS version
cmake --preset windows      # first run also builds Drogon/JsonCpp/SQLite3 via vcpkg — expect 15-30 minutes
cmake --build --preset windows --target localhistoria   # Release; Debug works but starts far slower (MSVC's checked iterators)
ctest --preset windows --output-on-failure   # optional
./build/Release/localhistoria.exe --llama-endpoint=http://127.0.0.1:8081
```

Or, as on macOS/Linux, start the model and the game together with one command (builds if needed, starts `llama-server.exe`, waits for it to load, opens the browser; Ctrl+C stops both):

```powershell
./start.ps1
./start.ps1 -NoOpen config/local-model.json
```

See [model setup](docs/MODELS.md#windows) for pointing it at a llama.cpp build and enabling CUDA offload (`-dev CUDA0`). `vcpkg.json` pins the third-party dependency versions; `CMakePresets.json`'s `windows` preset wires up the vcpkg toolchain.

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

### Nations, wars and treaties

- **National status.** Population, GDP and stability start from World Bank data (latest available year) plus documented estimates for territories it omits, split across regions. Annexed or seceding land takes its people and output with it. Each month populations and economies follow their trend. The model can nudge a nation's economy and stability qualitatively (from severe decline to strong growth); the game turns that into bounded numbers.
- **January 2026 world.** New campaigns start with the Russo-Ukrainian War, the Sudanese Civil War, the Myanmar Civil War and the Kivu Conflict under way, including Russian-held Crimea, Donetsk and Luhansk. NATO, the EU, the CSTO, AUKUS and other major treaties are already in force.
- **Named wars and treaties.** The model names new wars and treaties the way historians would (*Second Iberian War*, *Treaty of Porto*). Generic names are replaced and repeats are numbered. **Wars & treaties** in the header lists active wars, treaties and ended ones, with parties, dates, casualties, displacement and front regions.
- **War tolls.** Every month the game finds each war's front: regions changing hands, regions where units were destroyed, and bordering regions held by opposing parties. It applies deaths and displacement scaled by intensity and kind of war, calibrated against Russia–Ukraine and Sudan. Displaced people move to safer regions or cross into neighboring countries, and return once it is calm. The journal reports each war's monthly toll.
- **Map modes.** Switch the map between **Political** and **Wars & treaties**. The second shows the selected country's enemies, allies and treaty partners, other wars, active fronts by intensity, and occupied regions.

If the model is unreachable, times out, or returns an illegal or malformed result, the whole turn fails with no state change. Resubmit once the model is healthy. Narration is non-authoritative and may lag the saved turn; reload to see it. If narration fails, a factual event summary is kept. Inference is serialized. Adjudication reasons briefly before answering, so a month takes roughly one to two minutes on a 4B model.

## Optional Wikipedia reference

Campaign references prepare automatically at creation/resume and ahead of upcoming actors, on a separate bounded background queue. Turns use a frozen snapshot of available cache and never wait for downloads. Country references use ISO identity; regional references require a unique exact English label, country affiliation and administrative parent in Wikidata. Missing or ambiguous references permit conservative actions without invented history. Failed refreshes preserve valid cache. You can also explicitly refresh a country's reference from its inspector. Fictional countries never receive fabricated Wikipedia identities. Set `"reference_network": false` in the server JSON config to disable Wikimedia requests while continuing to use cached references.

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

## A living political world

Each month reserves three independent actions alongside reactions to your orders: two different non-player nations and one region (including regions of your own country). Country-balanced regional selection and two-month cooldown preferences reduce repetition. Empty orders still produce independent activity. Scarce candidate pools redistribute available slots, never exceeding three.

Regions retain agendas, relations and pending demands across control changes. See them in the governing country's inspector. Only an explicit national response can accept or reject a demand; a region cannot command national armies, sign treaties or rename its country. Independence requires national agreement or an established relevant conflict and validated land allocation. Autonomous armed rebellion is excluded.

New states, substantive renames and explicit constitutional/regime refoundings receive campaign flags. The model proposes a small geometric design; C++ renders allowlisted SVG and uses a deterministic fallback for invalid designs. Elections and ordinary political changes keep existing flags. Names update throughout the interface, actor IDs remain permanent, and the journal retains historical flag revisions.
