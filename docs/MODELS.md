# Local inference

A reachable llama-server is required; there is no offline/scripted mode. LocalHistoria never downloads or bundles model files.

Start with **Qwen3.5-4B, Q4_K_M GGUF** ([official model card](https://huggingface.co/Qwen/Qwen3.5-4B)). Install a current llama.cpp build with Qwen3.5 support, e.g. `brew install llama.cpp` on macOS. Then run:

```sh
llama serve \
  -hf unsloth/Qwen3.5-4B-GGUF \
  --alias local --host 127.0.0.1 --port 8081 \
  --ctx-size 32768 --parallel 1 --jinja \
  --cache-ram 1024 --ctx-checkpoints 8 \
  --chat-template-kwargs '{"enable_thinking":true}'
```

`-hf` makes llama.cpp itself download the default Q4_K_M file from the [unsloth GGUF repository](https://huggingface.co/unsloth/Qwen3.5-4B-GGUF) on first launch and cache it under `~/.cache/huggingface`. Review the model license and publisher. To use a local file instead, replace `-hf …` with `-m path/to/Qwen3.5-4B-Q4_K_M.gguf`.

`./start.sh` in the project root runs this command (with any `MODEL`, `MODEL_FILE`, `MODEL_PORT` or `CTX_SIZE` overrides) and the game together; see the README. To run them separately, in another terminal, from the project root:

```sh
./build/localhistoria --llama-endpoint=http://127.0.0.1:8081
```

The `--llama-endpoint=` flag sets the model endpoint for this launch and saves it in the campaign save, so later launches need no flag. You can also change the endpoint any time from the **Model endpoint** field in the web UI (`POST /api/v1/model-config`), which is also kept across restarts. Either way, only a loopback endpoint of the form `http://127.0.0.1:<port>` is accepted.

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

The app reads the real context size from `/props` and needs at least 8,192 tokens. Below 16,384 tokens it disables reasoning (see below). One slot (`--parallel 1`) is assumed; the app sends one request at a time.

## Thinking

The server enables thinking by default. The app sets `chat_template_kwargs.enable_thinking` per request:

- **Adjudication** (deciding what happens) thinks, with a `thinking_budget_tokens` of 256 per action plus 256, bounded to 512–2,048. Unbounded, the 4B model spent over 4,000 tokens (2+ minutes) reasoning about a single construction order. If a reasoning run produces no valid answer, adjudication is retried once without thinking.
- **AI nations' actions** and **narration** do not think, to keep a month's turn short.

## Requests

The adapter uses [`POST /v1/chat/completions`](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md) with:

- `response_format.type=json_schema`, temperature 0.6 for adjudication and 0.7 for AI actions and narration (at temperature 0 AI nations repeated the same actions every month);
- 768 output tokens reserved for decisions and narration;
- 2,048 answer tokens plus the reasoning budget reserved for adjudication.

The request accepts no tool calls or file access. The model alias is `local`. Reasoning text is kept in the inference audit (`GET /api/v1/inference?job=…`), never shown as game fact.

An AI nation's decision has the form `{"actions":[{"text":"Open trade talks with Portugal"}]}`, with at most three actions. Adjudication returns events with compact map operations; C++ validates them, enforces realistic pacing and derives the exact world delta. See [Evolving world](EVOLVING_WORLD.md). Any unreachable, timed-out, malformed or oversized result fails the turn outright: no scripted fallback, no partial state change. Resubmit once the model is healthy. Narration is requested as `{"text":"..."}` after the state commit. It is always labelled as AI narration. If it fails, the committed factual event summary is kept.

The app permits `0 < timeout_seconds ≤ 600` per request (default 240), set in the JSON config file passed as the server's positional argument.

The optional **Qwen3.5-9B** profile uses the same command with `-hf unsloth/Qwen3.5-9B-GGUF` ([official 9B model card](https://huggingface.co/Qwen/Qwen3.5-9B)). It uses more memory and is slower; its memory and speed have not been measured here.

Before every request, the adapter checks the server's context capacity via `/props`, renders the chat template with `/apply-template` and counts tokens with `/tokenize`. It then trims optional reference and history material until the request fits.
