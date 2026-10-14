$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$pythonExe = Join-Path $projectRoot '.venv/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $pythonExe)) {
    $pythonExe = Join-Path $projectRoot '.worktrees/implementation/.venv/Scripts/python.exe'
}
if (-not (Test-Path -LiteralPath $pythonExe)) { throw 'Instale primeiro as dependências com scripts/setup.ps1.' }
$env:PYTHONPATH = Join-Path $projectRoot 'src'
Push-Location -LiteralPath $projectRoot
try {
    & $pythonExe -m reworkbench.engine.config
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
