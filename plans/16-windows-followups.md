# 16 · Windows follow-ups after commit `ba07b84`

## Goal
Remove the machine-specific default, warn on MSVC, and make the Python test tooling find the Windows binary.

## Evidence
- `start.ps1:48` default `$LlamaDir = "C:\Users\PC\Downloads\llama-b11017-bin-win-cuda-13.4-x64"` (a
  personal path committed to the repo).
- `CMakeLists.txt:17–19` skips `-Wall -Wextra -Wpedantic` under MSVC and adds nothing.
- `tests/integration.py:55`, `tools/living_world_probe.py:20` use `build/localhistoria`; the Windows build
  produces `build/Release/localhistoria.exe`. `integration.py:125` calls `build/replay_verify`.
- `start.ps1:49` defaults `LLAMA_DEVICE` to `CUDA0`, which fails on CPU-only llama.cpp builds (documented).
- The commit message states a full model turn was not verified on Windows.

## Steps
1. `start.ps1`: default `$LlamaDir` to empty; resolve `llama-server.exe` from `$env:LLAMA_DIR`, then PATH,
   then the `llama` shim if present; error with the docs pointer otherwise. Update the comment block and
   `docs/MODELS.md` Windows section (remove the download-path sentence).
2. Device probe: if `$env:LLAMA_DEVICE` is unset, run `& $llamaExe --list-devices` and pick the first line
   containing `CUDA`; if none, use `none`. Keep the env override.
3. `CMakeLists.txt`: `if(MSVC) target_compile_options(historia_core PRIVATE /W4 /permissive- /utf-8) else() …`.
   Apply to `historia_services` too (today only `historia_core` has warnings enabled; add them for both).
4. Binary discovery in Python: add `tests/_paths.py` with `def binary(name)` that returns the first existing of
   `build/<name>`, `build/Release/<name>.exe`, `build/Debug/<name>.exe`; use it in `tests/integration.py`,
   `tools/living_world_probe.py`. `subprocess.Popen` with `cwd=ROOT` is fine on Windows.
5. `tests/integration.py` uses `ThreadingHTTPServer`, temp dirs and `sqlite3`: all portable. The server lock
   file test (kill and restart, line 143) relies on the OS releasing the handle; fine with `_SH_DENYRW`.
6. Run `python tests/integration.py` on Windows once (needs the built binaries and `node` + Playwright for the
   browser part; allow skipping the browser step with an env var `LOCALHISTORIA_SKIP_BROWSER=1`).
7. `.gitattributes`: add `*.ps1 text eol=crlf` so PowerShell scripts keep CRLF on checkout (some PS versions
   mis-parse LF-only here-strings; harmless otherwise).

## Acceptance
`start.ps1` has no personal paths; MSVC build shows warnings under `/W4` (fix any real ones); the integration
suite passes on Windows or its skipped parts are documented in `docs/VERIFICATION.md`.
