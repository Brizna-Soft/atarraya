# Junta las licencias de las bibliotecas que van dentro de Atarraya en AVISOS-DE-TERCEROS.txt.
# Las toma de lo que instaló vcpkg (build\vcpkg_installed\<triplet>\share\<biblioteca>\copyright).
param([string]$Triplet = 'x64-windows-static-rel')
$ErrorActionPreference = 'Stop'
$share = Join-Path $PSScriptRoot "build\vcpkg_installed\$Triplet\share"
if (-not (Test-Path $share)) { throw "Primero compila: no existe $share" }

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('AVISOS DE TERCEROS')
[void]$sb.AppendLine('==================')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('Atarraya usa estas bibliotecas de código abierto. Gracias a sus autores.')
[void]$sb.AppendLine('Abajo va el aviso de licencia de cada una, tal como lo publican.')
[void]$sb.AppendLine('')
foreach ($d in Get-ChildItem $share -Directory | Sort-Object Name) {
    if ($d.Name -like 'vcpkg-*') { continue }   # herramientas de compilación, no van en el programa
    $copyright = Join-Path $d.FullName 'copyright'
    if (-not (Test-Path $copyright)) { continue }
    [void]$sb.AppendLine('------------------------------------------------------------------------')
    [void]$sb.AppendLine($d.Name)
    [void]$sb.AppendLine('------------------------------------------------------------------------')
    [void]$sb.AppendLine((Get-Content $copyright -Raw -Encoding UTF8).TrimEnd())
    [void]$sb.AppendLine('')
}
$salida = Join-Path $PSScriptRoot 'AVISOS-DE-TERCEROS.txt'
[System.IO.File]::WriteAllText($salida, $sb.ToString(), (New-Object System.Text.UTF8Encoding($true)))
Write-Host "Listo: $salida"
