$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$environmentRoot = Join-Path $projectRoot '.venv'
$pythonExe = Join-Path $environmentRoot 'Scripts/python.exe'
if (-not (Test-Path -LiteralPath $pythonExe)) {
    & py -3.13 -m venv $environmentRoot
    if ($LASTEXITCODE -ne 0) { throw 'Não foi possível criar o ambiente com Python 3.13 de 64 bits.' }
}
Push-Location -LiteralPath $projectRoot
try {
    & $pythonExe -m pip install -e '.[engine]'
    if ($LASTEXITCODE -ne 0) { throw 'A instalação das dependências falhou.' }
} finally {
    Pop-Location
}
Write-Output 'Dependências instaladas. Configure Ghidra 12.1.4 e JDK 21 nas configurações do aplicativo.'
