param(
    [string]$Project,
    [string]$Sample
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$environmentRoot = Join-Path $projectRoot '.venv'
if (-not (Test-Path -LiteralPath (Join-Path $environmentRoot 'Scripts/python.exe'))) {
    $environmentRoot = Join-Path $projectRoot '.worktrees/implementation/.venv'
}
$pythonExe = Join-Path $environmentRoot 'Scripts/python.exe'
if (-not (Test-Path -LiteralPath $pythonExe)) {
    throw 'Ambiente Python não encontrado. Execute scripts/setup.ps1 quando desejar instalar as dependências.'
}
$env:PYTHONPATH = Join-Path $projectRoot 'src'
$launchArguments = @('-m', 'reworkbench')
if ($Project) { $launchArguments += $Project }
if ($Sample) { $launchArguments += @('--sample', $Sample) }
# Start only when this launcher is explicitly invoked. No compilation or engine
# provisioning is performed by this script.
Push-Location -LiteralPath $projectRoot
try {
    & $pythonExe @launchArguments
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
