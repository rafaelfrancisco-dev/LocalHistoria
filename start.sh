#!/usr/bin/env bash
# Start the local model (llama.cpp) and LocalHistoria together, then open the game.
#
#   ./start.sh                      build if needed, start both, open the browser
#   ./start.sh --no-open            do not open the browser
#   ./start.sh config/my.json       pass a server config (port, save_path, timeout_seconds)
#
# Environment overrides: MODEL (Hugging Face repo, default unsloth/Qwen3.5-4B-GGUF), MODEL_FILE (a local .gguf
# instead of MODEL), MODEL_PORT (8081), CTX_SIZE (32768). An already running model server on MODEL_PORT is reused
# and left running on exit; a model server started here is stopped with the game (Ctrl+C).
set -euo pipefail
cd "$(dirname "$0")"

MODEL="${MODEL:-unsloth/Qwen3.5-4B-GGUF}"
MODEL_PORT="${MODEL_PORT:-8081}"
CTX_SIZE="${CTX_SIZE:-32768}"
OPEN=1
CONFIG=""
for arg in "$@"; do
    case "$arg" in
        --no-open) OPEN=0 ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "Unknown option: $arg" >&2; exit 2 ;;
        *) CONFIG="$arg" ;;
    esac
done

GAME_PORT=8080
if [[ -n "$CONFIG" ]]; then
    [[ -f "$CONFIG" ]] || { echo "Config not found: $CONFIG" >&2; exit 2; }
    GAME_PORT="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("port", 8080))' "$CONFIG")"
fi
ENDPOINT="http://127.0.0.1:${MODEL_PORT}"
mkdir -p logs
MODEL_PID=""
GAME_PID=""
cleanup() {
    trap - EXIT INT TERM
    set +e
    for pid in "$GAME_PID" "$MODEL_PID"; do
        if [[ -n "$pid" ]]; then kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; fi
    done
    echo "Stopped."
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

# 1. Build (a no-op when up to date).
if [[ ! -f build/CMakeCache.txt ]]; then
    echo "Configuring build…"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix 2>/dev/null || true)" >/dev/null
fi
echo "Building…"
cmake --build build -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" --target localhistoria >/dev/null

# 2. Model server: reuse a running one, otherwise start llama.cpp.
if curl -sf -m 2 "$ENDPOINT/health" >/dev/null; then
    echo "Using the model server already running at $ENDPOINT."
else
    if command -v llama >/dev/null; then
        LLAMA=(llama serve)
    elif command -v llama-server >/dev/null; then
        LLAMA=(llama-server)
    else
        echo "llama.cpp not found. Install it (e.g. brew install llama.cpp); see docs/MODELS.md." >&2
        exit 1
    fi
    if [[ -n "${MODEL_FILE:-}" ]]; then SOURCE=(-m "$MODEL_FILE"); else SOURCE=(-hf "$MODEL"); fi
    echo "Starting the model (${MODEL_FILE:-$MODEL}) on $ENDPOINT; log: logs/llama.log"
    "${LLAMA[@]}" "${SOURCE[@]}" \
        --alias local --host 127.0.0.1 --port "$MODEL_PORT" \
        --ctx-size "$CTX_SIZE" --parallel 1 --jinja \
        --cache-ram 1024 --ctx-checkpoints 8 \
        --no-mmproj --reasoning on >logs/llama.log 2>&1 &
    MODEL_PID=$!
    # The first launch downloads the model, which can take several minutes.
    printf "Waiting for the model to load"
    until curl -sf -m 2 "$ENDPOINT/health" >/dev/null; do
        if ! kill -0 "$MODEL_PID" 2>/dev/null; then
            echo; echo "The model server exited. Last log lines:" >&2; tail -n 20 logs/llama.log >&2; MODEL_PID=""; exit 1
        fi
        printf "."; sleep 2
    done
    echo " ready."
fi

# 3. Game server.
echo "Starting LocalHistoria; log: logs/localhistoria.log"
./build/localhistoria ${CONFIG:+"$CONFIG"} --llama-endpoint="$ENDPOINT" >logs/localhistoria.log 2>&1 &
GAME_PID=$!
URL="http://127.0.0.1:${GAME_PORT}"
until curl -sf -m 2 "$URL/api/v1/campaign" >/dev/null; do
    if ! kill -0 "$GAME_PID" 2>/dev/null; then
        echo "LocalHistoria exited. Last log lines:" >&2; tail -n 20 logs/localhistoria.log >&2; GAME_PID=""; exit 1
    fi
    sleep 0.3
done
echo "LocalHistoria is running at $URL. Press Ctrl+C to stop."
if [[ "$OPEN" == 1 ]]; then
    if command -v open >/dev/null; then open "$URL"; elif command -v xdg-open >/dev/null; then xdg-open "$URL" >/dev/null 2>&1; fi
fi
# Stop everything if either server stops on its own.
while kill -0 "$GAME_PID" 2>/dev/null && { [[ -z "$MODEL_PID" ]] || kill -0 "$MODEL_PID" 2>/dev/null; }; do sleep 1; done
echo "A server stopped; see logs/." >&2
