# 15 · Two llama.cpp slots and concurrent decision calls (same 4B model)

Do plans 04 and 05 first. **No second model, no 9B**: the machine has no memory for it. This plan only makes
the existing Qwen3.5-4B server answer two requests at once. Abort criterion in step 1.

## Goal
Run decision and chronicle calls two at a time, and keep the adjudication system prefix cached between months.

## Evidence
- Month 2: 6 decision calls 38.6 s, 3 chronicle calls 36.4 s, all serialized by the `inference` mutex
  (`src/providers.cpp:42`) against a `--parallel 1` server (`start.sh:553`, `start.ps1:107`).
- `prompt_tokens_details.cached_tokens` was 0 on all 21 recorded calls.
- Generation 31–34 tok/s, prompt 330–360 tok/s. Batched decode of two sequences for a 4B model on Metal
  typically gives ~1.6–1.8× aggregate throughput.

## Steps
1. Memory check first. Start the server with `--parallel 2 --ctx-size 65536` (per-slot context stays 32,768;
   the app requires `default_generation_settings.n_ctx ≥ 16,384` for thinking, read from `/props`) and sample
   `vmmap --summary <pid>` during a 3-month `tools/living_world_probe.py` run, as `docs/MODELS.md` did.
   If the physical footprint exceeds ~5 GB (today 3.8 GB peak), try `--ctx-size 49152`; if still over, stop
   here and record the numbers in `docs/MODELS.md`; the rest of the plan is not worth memory pressure.
2. Provider concurrency: replace `std::mutex inference` with a slot counter (mutex + condition variable,
   `slots` from a constructor parameter `parallel`, default 1, read from config key `model_parallel`, default
   2). `audit` gets its own mutex. `takeAudit()` semantics unchanged.
3. Turn worker (`src/main.cpp:589–650`): run chronicle calls, then reactive decisions, then independent
   decisions, each batch with `std::async(std::launch::async, …)` limited to `parallel` in flight; collect
   futures in a vector and consume them in the original order so `intentions` and `independent` keep their
   order (the adjudication schema depends on it). `store.recordInference` under `mutex` per finished future.
4. Slot affinity: send `"id_slot": 0` for `adjudicate`/`narrate` and `1` for `decide`/`chronicle` (fall back
   to 0 when only one slot exists). Check that month 2's adjudication record shows `cached_tokens > 0`.
5. Flags in `start.sh`, `start.ps1`, `docs/MODELS.md` (`--parallel 2 --ctx-size 65536`; mention the app
   still works with `--parallel 1`, just serially). `docs/ARCHITECTURE.md`: "A provider mutex enforces one
   inference request at a time" → slots.

## Tests
- `tests/core_tests.cpp`: provider with `parallel=2` and a transport that sleeps 100 ms: two concurrent
  `decide` calls complete in < 180 ms; with `parallel=1` in > 190 ms.
- `tests/integration.py` unchanged (`ThreadingHTTPServer` handles concurrent mock calls).

## Acceptance
Decision + chronicle time per month drops ≥ 35 % on the same save; adjudication `cached_tokens > 0` from the
second month; footprint numbers recorded in `docs/MODELS.md`.
