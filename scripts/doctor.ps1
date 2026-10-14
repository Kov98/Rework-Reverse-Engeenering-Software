$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$candidates = @(
    (Join-Path $projectRoot 'build/bin/reworkbench.exe'),
    (Join-Path $projectRoot 'native/workbench/build/bin/reworkbench.exe')
)
$nativeExe = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
[ordered]@{
    product = 'REWorkbench Native'
    native_executable = $nativeExe
    cmake_path = $(if ($cmakeCommand) { $cmakeCommand.Source } else { $null })
    requires = @('C++20 compiler', 'Qt6 >=6.5 Core/Widgets/Concurrent/Sql with QSQLITE', 'optional Capstone5')
    runtime_verified = $false
    ghidra_required = $false
} | ConvertTo-Json -Depth 3
if (-not $nativeExe) { exit 1 }
