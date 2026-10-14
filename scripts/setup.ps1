$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Write-Output 'REWorkbench agora e um aplicativo proprio C++20/Qt.'
Write-Output 'Preparacao futura: compilador C++20, CMake >=3.20, Qt >=6.5 (Core, Widgets, Concurrent, Sql, plugin QSQLITE).'
Write-Output 'Capstone5 e opcional para decodificacao; sem ele ficam formatos, bytes e strings.'
Write-Output 'Nenhuma dependencia e instalada e nenhum build e executado por este script.'
Write-Output "Consulte o README em $projectRoot para os comandos da etapa de compilacao futura."
