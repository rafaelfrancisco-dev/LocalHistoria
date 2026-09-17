# Local inference

A reachable llama-server is required — there is no offline/scripted mode. Model files are never downloaded or bundled by LocalHistoria.

Start with **Qwen3.5-4B, Q4_K_M GGUF**. The [official Qwen 4B model card](https://huggingface.co/Qwen/Qwen3.5-4B) describes the base model; [this GGUF publisher](https://huggingface.co/bartowski/Qwen_Qwen3.5-4B-GGUF) provides quantized files. Download the Q4_K_M file yourself, review its model license and publisher, and place it in ignored `models/`. Use a current llama.cpp build with Qwen3.5 support. Install separately, for example `brew install llama.cpp` on macOS.

```sh
llama-server \
  -m models/Qwen3.5-4B-Q4_K_M.gguf \
  --alias local --host 127.0.0.1 --port 8081 \
  --ctx-size 8192 --parallel 1 --jinja \
  --chat-template-kwargs '{"enable_thinking":false}'
```

Use the actual filename you downloaded. In another terminal, from the project root:

```sh
./build/localhistoria --llama-endpoint=http://127.0.0.1:8081
```

The `--llama-endpoint=` flag sets the model endpoint for this launch and persists it in the campaign save, so later launches need no flag. The endpoint can also be set or changed at any time from the **Model endpoint** field in the web UI (`POST /api/v1/model-config`), which likewise persists across restarts. Only a loopback endpoint of the form `http://127.0.0.1:<port>` is accepted, either way.

The optional **Qwen3.5-9B Q4_K_M** profile uses the same command with the 9B model path and the same 8K context, single slot and non-thinking setting. See the [official 9B model card](https://huggingface.co/Qwen/Qwen3.5-9B). It uses more memory and may require a longer timeout; no speed or memory guarantee is claimed. The app permits 0 < timeout ≤ 60 seconds; a `timeout_seconds` value can be set in the JSON config file passed as the server's positional argument.

The adapter uses [`POST /v1/chat/completions`](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md), `response_format.type=json_schema`, `chat_template_kwargs.enable_thinking=false`, temperature 0 and at most 768 output tokens. It accepts no tool calls, file access or model-authored state. The model alias is `local`. Only a loopback HTTP model endpoint is accepted. There is at most one inference request at a time, and no per-turn cap on how many decisions a turn may require.

A legal model response has the form `{"orders":[{"action":"economy"}]}`. Empty arrays are valid. Any unreachable, timed-out, malformed, unaffordable, illegal or oversized result fails the turn or proposal outright — no scripted fallback, no partial state change; resubmit once the model is healthy. Narration is separately requested as `{"text":"..."}` after the authoritative state commit; it is always labelled as AI narration, and its own failure (unlike a decision's) still preserves the already-committed factual event summary rather than failing the turn, since it carries no decision-making of its own.

The application bounds UTF-8 input bytes conservatively rather than assuming an average token ratio. Cached excerpts and recent events are clipped first; oversized dense diplomatic contexts fall back. This preserves room for system/template overhead and generated output under the documented 8K setup.

Verification in this checkout uses injected transport and a real loopback HTTP fixture server. No suitable local weights or llama-server installation were present during implementation, so **real Qwen inference has not been tested**.
