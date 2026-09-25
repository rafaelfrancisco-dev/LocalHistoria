# Local inference

A reachable llama-server is required; there is no offline/scripted mode. LocalHistoria never downloads or bundles model files.

Start with **Qwen3.5-4B, Q4_K_M GGUF** ([official model card](https://huggingface.co/Qwen/Qwen3.5-4B)). Install a current llama.cpp build with Qwen3.5 support, e.g. `brew install llama.cpp` on macOS. Then run:

```sh
llama serve \
  -hf unsloth/Qwen3.5-4B-GGUF \
  --alias local --host 127.0.0.1 --port 8081 \
  --ctx-size 32768 --parallel 1 --jinja \
  --cache-ram 1024 --ctx-checkpoints 8 \
  --no-mmproj --reasoning on
```

`--no-mmproj` skips the image projector that `-hf` otherwise downloads and loads; the game sends text only. `--reasoning on` replaces the deprecated `--chat-template-kwargs '{"enable_thinking":true}'`.

`-hf` makes llama.cpp itself download the default Q4_K_M file from the [unsloth GGUF repository](https://huggingface.co/unsloth/Qwen3.5-4B-GGUF) on first launch and cache it under `~/.cache/huggingface`. Review the model license and publisher. To use a local file instead, replace `-hf …` with `-m path/to/Qwen3.5-4B-Q4_K_M.gguf`.

`./start.sh` in the project root runs this command (with any `MODEL`, `MODEL_FILE`, `MODEL_PORT` or `CTX_SIZE` overrides) and the game together; see the README. To run them separately, in another terminal, from the project root:

```sh
./build/localhistoria --llama-endpoint=http://127.0.0.1:8081
```

### Windows

`./start.ps1` is the Windows sibling of `start.sh`: same overrides (`$env:MODEL`, `$env:MODEL_FILE`, `$env:MODEL_PORT`, `$env:CTX_SIZE`), plus `$env:LLAMA_DIR` (folder containing `llama-server.exe`) and `$env:LLAMA_DEVICE`. A prebuilt CUDA release of llama.cpp (e.g. `llama-b11017-bin-win-cuda-*-x64` from the [llama.cpp releases](https://github.com/ggml-org/llama.cpp/releases)) has no `llama` shim command, only `llama-server.exe`, and does not put itself on `PATH`, so point `LLAMA_DIR` at the extracted folder — `start.ps1` defaults it to a specific download path; set the env var if yours lives elsewhere:

```powershell
$env:LLAMA_DIR = "C:\path\to\llama-b11017-bin-win-cuda-13.4-x64"
./start.ps1
```

To offload to a GPU, pass its device name from `--list-devices` (e.g. `llama-server.exe --list-devices`) via `-dev`; `start.ps1` does this for you with `$env:LLAMA_DEVICE`, defaulting to `CUDA0` (the first CUDA device). Set `$env:LLAMA_DEVICE = "none"` for a CPU-only run (llama.cpp's own value meaning "don't offload"; CPU-only builds have no CUDA device and should use this). llama-server logs to stderr, captured in `logs\llama.err.log`; confirm the context llama.cpp actually granted by requesting `/props` and reading `default_generation_settings.n_ctx` — on a card with only a few GB free, `--fit` (on by default) may trim context or keep part of the model on the CPU before it lets context size suffer, which is slower but not broken (below 16,384 it silently disables reasoning, see below).

To run the pieces separately instead:

```powershell
./build/Release/localhistoria.exe --llama-endpoint=http://127.0.0.1:8081
```

Building the C++ server itself needs Visual Studio (Desktop C++ workload) and vcpkg for Drogon/JsonCpp/SQLite3; see the Windows section of the README. Build **Release**, not Debug: MSVC's Debug STL adds checked-iterator overhead to every container operation, which turns the startup load of region/geometry data from data/world.sqlite into a multi-minute stall instead of a few seconds — it looks like a hang, but it is not one.

The `--llama-endpoint=` flag sets the model endpoint for this launch and saves it in the campaign save, so later launches need no flag. You can also change the endpoint any time from the **Model endpoint** field under **Settings** in the web UI (`POST /api/v1/model-config`), which is also kept across restarts. Either way, only a loopback endpoint of the form `http://127.0.0.1:<port>` is accepted.

## Context size and memory

These settings fit a 16 GB Mac with the model kept under 8 GB:

- **`--ctx-size 32768`.** Qwen3.5 is a hybrid model: only a quarter of its layers keep a growing attention cache, so a large context is cheap. Prompts are 1,000–4,000 tokens, so this leaves ample room for long campaign histories.
- **`--cache-ram 1024 --ctx-checkpoints 8`.** llama.cpp otherwise keeps up to 8 GB of past prompts in RAM (entries of 250–400 MB each, plus up to 32 checkpoints per slot). LocalHistoria's prompts rarely share long prefixes, so that cache buys little.

Measured on a 16 GB Apple silicon MacBook Pro, llama.cpp build 11011, with `vmmap --summary` sampled every 10 s over the probe and a three-turn campaign:

| Settings | Physical footprint | Plus mapped weights |
| --- | --- | --- |
| 32K context, default caches, fresh | 2.0 GB | 2.5 GB |
| 32K context, default caches, after ~40 requests | 10.2 GB (mostly swapped) | 2.5 GB |
| 32K context, `--cache-ram 1024 --ctx-checkpoints 8`, peak | 3.8 GB | 2.5 GB |
| 64K context, `--parallel 2` (32K per slot), same caches, peak with two requests at once | 4.4 GB | 2.5 GB |

The app reads the real context size from `/props` and needs at least 8,192 tokens. Below 16,384 tokens it disables reasoning (see below). One slot (`--parallel 1`) is assumed; the app sends one request at a time.

Two slots do not make a month faster on this machine, so the app does not use them. Measured with llama.cpp build 11011 on the M1 Pro, `--parallel 2 --ctx-size 65536` (with or without `--no-kv-unified`): two requests at once each read prompts at 178 tokens/s and write 18 tokens/s, half the single-request speeds, so two 1,400-word prompts with 400 output tokens took 30.1 s together against 31.8 s one after the other. With decisions and chronicles sent two at a time, the time before adjudication over three probe months was 27.5, 50.8 and 33.9 s, against 25.9, 51.9 and 37.1 s serially. Prompt caching does not help adjudication either: `usage.prompt_tokens_details.cached_tokens` was 0 on every adjudication request, while decisions reused 215–224 tokens of their shared opening.

The macOS/Linux game launcher now builds `RelWithDebInfo` in `build/`, including when
that directory previously held Debug. Use `BUILD_TYPE=Debug ./start.sh` for an isolated
`build-debug/` diagnosis build; `BUILD_DIR` can select another directory. This changes
only the C++ application build, not model settings or the single inference slot.
The historical measurements above and below predate this change; the application build
type was not recorded alongside those runs. They are not Debug/optimized comparisons.

## Speed

On a 16 GB M1 Pro, the 4B Q4_K_M model reads prompts at about 350 tokens/s and writes about 34 tokens/s, so a month's time is mostly generated tokens, then prompt tokens. `llama bench -p 4096 -n 128` found no gain from flash attention (`-fa`) or a larger micro-batch (`-ub 1024` or `2048`, slightly slower). Context size and KV cache type do not change speed either: work scales with the tokens actually used, Qwen3.5's hybrid layers keep the cache small, and a quantized cache is not faster on Metal. So the app keeps requests short:

- References keep the **end** of each article's History section (the most recent period, up to 3,000 characters, citation markers removed). References cached before this was added are fetched again once.
- Each AI decision reads up to 1,200 characters of the introduction and the most recent 1,200 of the history.
- Adjudication reads references only for the player, nations at war, nations whose action this month is about war, military moves, borders, territory, independence, sanctions or treaties, and the nations those actions name. Each of these gets its introduction cut to 600 characters and the most recent 1,200 characters of its chronicle (600 of the raw history when it has none), plus its 8 latest related events and 4 latest past events. Every other nation gets no reference, no entity list, no regional politics, 4 related events and 2 past events. Narration receives no reference.
- Adjudication lists cities only in regions an action names or on a war's front; armies, fleets, bases, ports and event sites are listed in every listed region.
- The adjudication schema defines each operation once under `$defs` and references it from every outcome, instead of repeating the whole operations list, with its region and entity enumerations, in each one (a 13-action month's schema was 194 KB).
- Each adjudicated event opens with only the first six words of its action, not the whole text. Without any opening, the model repeated one outcome for all of a nation's actions.

Measured on the same first month of a Brazil campaign: about 250 s before these changes (adjudication: 11.2K prompt tokens, 4.4K generated, 179 s) and 120–160 s after (6.6–8K prompt tokens, 2.1–3.1K generated, 87–123 s; generation length varies from run to run).

## Thinking

The server enables thinking by default. The app sets `chat_template_kwargs.enable_thinking` per request:

- **Adjudication** (deciding what happens) thinks only when at least one action of the month is about war, military moves, borders, territory, independence, sanctions or treaties, with a `thinking_budget_tokens` of 128 per such action plus 256, bounded to 384–768. A month of reforms, talks and trade is answered without thinking. Unbounded, the 4B model spent over 4,000 tokens (2+ minutes) reasoning about a single construction order. If a reasoning run produces no valid answer, adjudication is retried once without thinking.
- **AI nations' actions** and **narration** do not think, to keep a month's turn short.

## Requests

The adapter uses [`POST /v1/chat/completions`](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md) with:

- `response_format.type=json_schema`, temperature 0.6 for adjudication and 0.7 for AI actions and narration (at temperature 0 AI nations repeated the same actions every month);
- 768 output tokens reserved for decisions and narration;
- 768 answer tokens plus 160 per adjudicated action (bounded to 1,536–4,096), plus the reasoning budget when thinking, reserved for adjudication. Each outcome's description is limited to 160 characters.

The request accepts no tool calls or file access. The model alias is `local`. Reasoning text is kept in the inference audit (`GET /api/v1/inference?job=…`), never shown as game fact.

An AI nation's decision has the form `{"actions":[{"text":"Open trade talks with Portugal"}]}`, with one action, or two for a nation the player named or is fighting. Adjudication returns events with compact map operations; C++ validates them, enforces realistic pacing and derives the exact world delta. See [Evolving world](EVOLVING_WORLD.md). Any unreachable, timed-out, malformed or oversized result fails the turn outright: no scripted fallback, no partial state change. Resubmit once the model is healthy. Narration is requested as `{"text":"..."}` after the state commit. It is always labelled as AI narration. If it fails, the committed factual event summary is kept.

The app permits `0 < timeout_seconds ≤ 600` per request (default 240), set in the JSON config file passed as the server's positional argument. `turn_budget_seconds` (default `2 × timeout_seconds`, at most 1800) bounds a whole month: each request's timeout is capped at what is left of it, and a month that runs out fails with "Month exceeded its time budget" and no state change. If the budget runs out after the month is committed, only the narration is skipped. The player can also cancel a running month from the UI (see [API](API.md)); the request in flight is dropped rather than waited for. With llama.cpp build 11011 the server logged `stop: cancel task` and freed its slot about 1.5 s after a cancel (it checks the client connection about once a second).

The optional **Qwen3.5-9B** profile uses the same command with `-hf unsloth/Qwen3.5-9B-GGUF` ([official 9B model card](https://huggingface.co/Qwen/Qwen3.5-9B)). It uses more memory and is slower; its memory and speed have not been measured here.

Before every request, the adapter checks the server's context capacity via `/props` (cached for 60 seconds). If the request is well inside the context (a byte estimate of about 3.2 bytes per token, plus the output reserve, at most 80% of it), it is sent at once. Otherwise the adapter renders the chat template with `/apply-template`, counts tokens with `/tokenize` and trims optional reference and history material until the request fits. The audit records the server's own prompt token count (`usage.prompt_tokens`) when it reports one, and `prompt_tokens_estimated` when the tokenizer was skipped.

Military context is deterministic: national status includes a readable inventory, uncommitted troops, field formations and a strength label. Region `within_reach_of` strings retain the actor code and add effective attack/defence strength and a zero/one/two-region limit. A `too weak` entry requires a failed, aborted or symbolic outcome. Entity facts carry personnel/ships; the model never selects formation sizes or casualty numbers.

## Measured prompt changes

Plan 26 phase D added two short glosses to the adjudication instructions: the `grant_access` treaty
clause, and armies sailing with a fleet in their region while fleets sail up to 1,500 km of sea. The
adjudication block is sensitive to added text on the 4B model, so both were A/B tested on 23
September 2026 against the previous prompt, with Qwen3.5-4B, identical binaries except
`prompts.cpp`, and three `world_probe` runs per scenario:

| Scenario | New prompt | Previous prompt |
|---|---|---|
| `access_treaty` (proposal with a `grant_access` term) | 2/3 | 0/3 |
| `sea_move` (fleet to an island) | 3/3 | 3/3 |
| `overseas_landing` (army sails with the fleet) | 3/3 | 2/3 |
| `conflict`, `treaty_consent`, `invasion` (regression) | 2/3, 2/3, 3/3 | 2/3, 2/3, 3/3 |

Latency was the same (17–22 s per scenario). Two months played from one copied save on each prompt
showed no echo of the previous month: each month's events answered that month's orders. Both
prompts still often narrate a treaty offer or acceptance without a treaty operation, so the engine
reports "No validated change resulted from the proposed action".

Plan 26 phase J added facts, not instructions: the nation's `implementation` line, a `forces`
exhaustion phrase once it matters, and `conquests` for annexed land still held by force. The
adjudication instructions are unchanged. It was measured on 23 September 2026 with Qwen3.5-4B by
playing the same two months from one copied save on the new binary and on the previous commit:

- **Prompt size:** the month-2 decision prompts, the first to carry the new line, grew by 19–74
  tokens (1–4%).
- **Latency:** unchanged (3–14 s per decision, 84–86 s per adjudication).
- **Outcomes:** month 2 had the same outcomes on both sides, and neither echoed month 1.

The Plan 26 re-audit (23 September 2026) measured both schemas on Qwen3.5-4B. It used a six-month
Ukraine campaign with identical orders on a disposable save, and `world_probe` × 3:

| | Before the fixes | After |
|---|---|---|
| Model events blanked to "No validated change" | 37 of 60 | 4 of 65 |
| `outcome_kind` / `independent_outcome` given | 0 of 58 (optional) | 57 of 57 (required, after `operations`) |
| AI answers to treaty proposals | none (acceptances written as prose) | 5 structured answers; 2 AI–AI treaties formed |
| Player treaty orders that became proposals | 0 of 3 | 1 of 3 (the other two kept as narrated negotiations) |
| Reform efforts started | 0 | 1 (continued, not restarted, by repeat orders) |
| Adjudication per month | 104–134 s, 1,887–2,452 output tokens | 120–129 s, 2,010–2,859 tokens; one month hit the output cap and retried (+49 s) |
| `world_probe` reform (effort started) | 0/3 | 3/3 |

Placing the required field before `operations` made the model label treaty offers "negotiation"
and then omit the treaty operation. After `operations`, operation choice is unchanged. The output
allowance per outcome rose from 160 to 192 tokens after the capped month. That change is only a
ceiling and was not re-measured live. A negotiation outcome keeps its text, so "Ukraine formally
submits a proposal for a ceasefire" can read like an offer that the engine never recorded; the
proposals list and the treaty panel show only real offers. Decision schemas that offer proposals now require a
`responses` object with one boolean per proposal ID.

Plan 28 phase A (event text version 2) replaced the adjudication sentence about `outcome_kind` and
`effects` rather than adding one. `none` gave way to `security` and `policy`, and "Omit effects
unless something changes" became "Every outcome has a consequence: rate it from the facts and
history, a minor act a minor effect". The schema requires at least one rated effect, which must
move an axis (an entry may not be `none`/`none`). It is named `outcome_effects` so that it sorts
after `operations`, and the provider renames it back to `effects`. It was measured on 24 September
2026 with Qwen3.5-4B and `world_probe`, three runs per scenario:

| Scenario | Version 2 (final) | Previous |
|---|---|---|
| `border_police` (a deployment, no map change): text kept, model-rated | 3/3 | 0/3 (one blanked, one relabelled as talks, one reform) |
| `talks` (preliminary talks): text kept, model-rated | 3/3 | 0/3 (all replaced by "Preliminary diplomatic talks…") |
| `reform`: effort started | 2/3 (one labelled `policy`) | 3/3 |
| `access_treaty`: proposal with a `grant_access` term | 0/3 | 1/3 |
| `treaty_consent`: an AI pact becomes a proposal | 1/3 | 2/3 |
| `conflict`: the ordered occupation | 3/3 | 1/3 |
| `invasion` (strong attacker): the ordered occupation | 3/3 | 3/3 |

- **Blanking:** the previous prompt replaced the text of 9 of its 21 outcomes with "No validated
  change" or "Preliminary diplomatic talks"; version 2 replaced none.
- **Default effects:** none of the 21 outcomes needed the C++ default. The two intermediate
  variants show why both schema choices matter:
  - Required effects alone came back as a single `none`/`none` entry in 4 of 21 probe outcomes
    and 8 of 19 outcomes in a live month, so C++ supplied the consequence.
  - Requiring an axis to move, but with the field sorted before `operations` (`effects`), cut
    the ordered treaty and war operations to 4 of 12. Written after the operations, as with
    `outcome_kind`, it is 7 of 12, the same as the previous prompt.
- **Treaties:** version 2 wrote fewer treaty operations here (1 of 6 against 3 of 6). The small
  model has always narrated offers without writing them (see above). What changed is how that is
  reported: the text is kept, and any outcome without a successful operation whose description
  narrates a signing, treaty, pact, accord, agreement or alliance ends "No agreement was recorded."
  (in two runs of an intermediate variant the model narrated a pact as signed). Three runs a
  scenario cannot separate this from noise.
- **Reform labelled `policy`:** without a map change a `policy` or `security` outcome may cost the
  economy but never grow it, so a reform the model files as a policy earns no growth without the
  effort that pays for it.
- **Latency:** unchanged (17–25 s for operational scenarios, 4–8 s for talks and reform, on both
  sides).


### Plan 31 project and consequence contract

With project version 1, adjudication has a shared `domestic_project` schema definition. Every
outcome supplies `project: null` or `{action, id, objective, target, benefit}`. `start` uses an
empty ID; `continue`/`cancel` use an existing actor-owned ID and unchanged terms from context.
Reform purposes are `administration` and `public_services`; investment purposes are
`infrastructure` and `production`. `legacy` is only for continuing migrated work. C++ owns IDs,
prices, funding, progress and benefits. An event-site marker may accompany the project but cannot
substitute for its funded record. No extra inference request is introduced.

Consequence version 1 allows empty or `none`/`none` ratings. C++ applies conservative per-kind
monthly limits and diminishing repetition weights, to positive and negative ratings alike.
Free-text descriptions cannot prove a project complete or grant unsupported benefits. Both
nation decisions and adjudication see bounded authoritative project summaries, including paid
months, delays and completion. See `docs/PLAN_31.md` for measured validation and limitations.

### Plan 32 regional demand contract

With demand version 1, `regional_initiative` requires `demand_kind` (`political`,
`regional_project`, `independence`, `none`) and `project_benefit` (a plan 31 purpose or `none`).
Both are required because the 4B model leaves optional fields out, and `none` is listed last
because it tends to answer a required enum with its first value. The instruction adds one clause
naming the three kinds. C++ derives every term: category, cost, months, the new state's name,
regions and required consent. An empty `demand` text with a funded or independence kind gets a
generated one. The schema grows by 244 bytes for these two fields. A typed demand adds up to
about 300 bytes to `regional_politics` facts, which only central actors receive. Acceptance and
fulfilment need no extra decision or adjudication call. A two-month A/B against the previous
prompt showed no echo regression. The model filled both fields but chose only `political` in two
samples, and once wrote `demand: "none"`, which C++ now treats as no demand (see
`docs/PLAN_32.md`).

Plan 34 measurement tooling now uses WAL-aware disposable snapshots, replay-before-timing,
210-second default month budgets and alternating paired experiment runs. See
[Plan 34 status and manifest](PLAN_34.md). No new live-model latency or operation-success
measurement is established by this tooling change; all three reductions remain opt-in.
