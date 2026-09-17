<#
.SYNOPSIS
    Start the local model (llama.cpp) and LocalHistoria together on Windows, then open the game.

.DESCRIPTION
    Windows sibling of start.sh. Builds the project if needed (CMake + the "windows" preset, which
    uses vcpkg for Drogon/JsonCpp/SQLite3 — see docs/MODELS.md), starts llama-server.exe, waits for
    it to load, starts the game server, and opens the browser. Ctrl+C stops both.

.PARAMETER Config
    Optional server config file (port, save_path, timeout_seconds), e.g. config/local-model.json.

.PARAMETER NoOpen
    Do not open the browser.

.EXAMPLE
    ./start.ps1
.EXAMPLE
    ./start.ps1 -NoOpen config/local-model.json
.EXAMPLE
    $env:MODEL_FILE = "C:\models\Qwen3.5-4B-Q4_K_M.gguf"; ./start.ps1

.NOTES
    Environment overrides:
      MODEL        Hugging Face repo (default unsloth/Qwen3.5-4B-GGUF)
      MODEL_FILE   a local .gguf instead of MODEL
      MODEL_PORT   default 8081
      CTX_SIZE     default 32768
      LLAMA_DIR    folder containing llama-server.exe (default: the CUDA build under Downloads;
                   falls back to PATH if not found there)
      LLAMA_DEVICE device(s) passed to llama-server's -dev flag (default: CUDA0; set to "none" for
                   a CPU-only run, llama.cpp's own value meaning "don't offload")
    An already running model server on MODEL_PORT is reused and left running on exit; a model
    server started here is stopped with the game (Ctrl+C).
#>
param(
    [Parameter(Position = 0)]
    [string]$Config,
    [switch]$NoOpen
)

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

$Model = if ($env:MODEL) { $env:MODEL } else { "unsloth/Qwen3.5-4B-GGUF" }
$ModelPort = if ($env:MODEL_PORT) { $env:MODEL_PORT } else { "8081" }
$CtxSize = if ($env:CTX_SIZE) { $env:CTX_SIZE } else { "32768" }
$LlamaDir = if ($env:LLAMA_DIR) { $env:LLAMA_DIR } else { "C:\Users\PC\Downloads\llama-b11017-bin-win-cuda-13.4-x64" }
$LlamaDevice = if ($env:LLAMA_DEVICE) { $env:LLAMA_DEVICE } else { "CUDA0" }

$GamePort = 8080
if ($Config) {
    if (-not (Test-Path $Config)) { Write-Error "Config not found: $Config"; exit 2 }
    $cfg = Get-Content $Config -Raw | ConvertFrom-Json
    if ($cfg.port) { $GamePort = $cfg.port }
}
$Endpoint = "http://127.0.0.1:$ModelPort"
New-Item -ItemType Directory -Force -Path logs | Out-Null

$ModelProcess = $null
$GameProcess = $null
function Stop-All {
    foreach ($p in @($GameProcess, $ModelProcess)) {
        if ($p -and -not $p.HasExited) {
            try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch {}
        }
    }
    Write-Host "Stopped."
}

try {
    # 1. Build (a no-op when up to date).
    $cmake = "cmake"
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        $vsCmake = "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $vsCmake) { $cmake = $vsCmake }
        else { Write-Error "cmake not found. Install CMake or Visual Studio (see docs/MODELS.md)."; exit 1 }
    }
    if (-not $env:VCPKG_ROOT) {
        $vsVcpkg = "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\VC\vcpkg"
        if (Test-Path $vsVcpkg) { $env:VCPKG_ROOT = $vsVcpkg }
    }
    if (-not (Test-Path "build\CMakeCache.txt")) {
        Write-Host "Configuring build..."
        & $cmake --preset windows | Out-Null
    }
    Write-Host "Building..."
    & $cmake --build --preset windows --target localhistoria | Out-Null

    # 2. Model server: reuse a running one, otherwise start llama-server.exe.
    $healthy = $false
    try { $null = Invoke-WebRequest -Uri "$Endpoint/health" -TimeoutSec 2 -UseBasicParsing; $healthy = $true } catch {}
    if ($healthy) {
        Write-Host "Using the model server already running at $Endpoint."
    } else {
        $llamaExe = Join-Path $LlamaDir "llama-server.exe"
        if (-not (Test-Path $llamaExe)) {
            $onPath = Get-Command llama-server.exe -ErrorAction SilentlyContinue
            if ($onPath) { $llamaExe = $onPath.Source }
            else { Write-Error "llama-server.exe not found in `$LLAMA_DIR ($LlamaDir) or on PATH. See docs/MODELS.md."; exit 1 }
        }
        $sourceArgs = if ($env:MODEL_FILE) { @("-m", $env:MODEL_FILE) } else { @("-hf", $Model) }
        $label = if ($env:MODEL_FILE) { $env:MODEL_FILE } else { $Model }
        Write-Host "Starting the model ($label) on $Endpoint; log: logs\llama.err.log (llama-server logs to stderr)"
        $llamaArgs = $sourceArgs + @(
            "--alias", "local", "--host", "127.0.0.1", "--port", $ModelPort,
            "--ctx-size", $CtxSize, "--parallel", "1", "--jinja",
            "--cache-ram", "1024", "--ctx-checkpoints", "8",
            "--no-mmproj", "--reasoning", "on",
            "-dev", $LlamaDevice
        )
        $ModelProcess = Start-Process -FilePath $llamaExe -ArgumentList $llamaArgs -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput "logs\llama.log" -RedirectStandardError "logs\llama.err.log"
        Write-Host -NoNewline "Waiting for the model to load"
        while (-not $healthy) {
            if ($ModelProcess.HasExited) {
                Write-Host ""
                Write-Error "The model server exited. Last log lines:"
                Get-Content "logs\llama.err.log" -Tail 20 -ErrorAction SilentlyContinue
                $ModelProcess = $null
                exit 1
            }
            try { $null = Invoke-WebRequest -Uri "$Endpoint/health" -TimeoutSec 2 -UseBasicParsing; $healthy = $true }
            catch { Write-Host -NoNewline "."; Start-Sleep -Seconds 2 }
        }
        Write-Host " ready."
    }

    # 3. Game server.
    Write-Host "Starting LocalHistoria; log: logs\localhistoria.log"
    $gameArgs = @()
    if ($Config) { $gameArgs += $Config }
    $gameArgs += "--llama-endpoint=$Endpoint"
    $GameProcess = Start-Process -FilePath "build\Release\localhistoria.exe" -ArgumentList $gameArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput "logs\localhistoria.log" -RedirectStandardError "logs\localhistoria.err.log"
    $Url = "http://127.0.0.1:$GamePort"
    $ready = $false
    while (-not $ready) {
        if ($GameProcess.HasExited) {
            Write-Error "LocalHistoria exited. Last log lines:"
            Get-Content "logs\localhistoria.err.log" -Tail 20 -ErrorAction SilentlyContinue
            $GameProcess = $null
            exit 1
        }
        try { $null = Invoke-WebRequest -Uri "$Url/api/v1/campaign" -TimeoutSec 2 -UseBasicParsing; $ready = $true }
        catch { Start-Sleep -Milliseconds 300 }
    }
    Write-Host "LocalHistoria is running at $Url. Press Ctrl+C to stop."
    if (-not $NoOpen) { Start-Process $Url }

    # Stop everything if either server stops on its own.
    while ((-not $GameProcess.HasExited) -and ($null -eq $ModelProcess -or -not $ModelProcess.HasExited)) {
        Start-Sleep -Seconds 1
    }
    Write-Host "A server stopped; see logs\." -ForegroundColor Yellow
} finally {
    Stop-All
}
