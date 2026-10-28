# Arma las dos versiones de la extensión (Chrome/Edge/Brave y Firefox) en la carpeta indicada.
param([Parameter(Mandatory = $true)][string]$Destino)
$ErrorActionPreference = 'Stop'
foreach ($navegador in 'chrome', 'firefox') {
    $carpeta = Join-Path $Destino $navegador
    New-Item -ItemType Directory -Force $carpeta | Out-Null
    Copy-Item (Join-Path $PSScriptRoot 'comun\*') $carpeta -Recurse -Force
    Copy-Item (Join-Path $PSScriptRoot "$navegador\manifest.json") $carpeta -Force
}
