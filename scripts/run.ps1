param(
    [string]$Project,
    [string]$Sample
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$candidates = @(
    (Join-Path $projectRoot 'build/bin/reworkbench.exe'),
    (Join-Path $projectRoot 'native/workbench/build/bin/reworkbench.exe')
)
$nativeExe = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $nativeExe) {
    throw 'O aplicativo C++ ainda nao foi compilado. Consulte README.md. Este script nao compila e nao inicia o prototipo Ghidra.'
}
$launchArguments = @()
if ($Project) { $launchArguments += @('--project', (Resolve-Path -LiteralPath $Project).ProviderPath) }
if ($Sample) { $launchArguments += @('--sample', (Resolve-Path -LiteralPath $Sample).ProviderPath) }
# This launcher starts only an existing native executable.
Push-Location -LiteralPath $projectRoot
try {
    & $nativeExe @launchArguments
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
