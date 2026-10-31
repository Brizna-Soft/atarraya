# Arma publicar\Atarraya-<versión>-x64.zip (programa, puente, extensión y licencias) y su .sha256.
# Antes hay que compilar con compilar.ps1.
$ErrorActionPreference = 'Stop'
$raiz = $PSScriptRoot
$version = (Get-Content (Join-Path $raiz 'version.txt') -TotalCount 1).Trim()
$programa = Join-Path $raiz 'programa'
$publicar = Join-Path $raiz 'publicar'
$nombre = "Atarraya-$version-x64"
$temporal = Join-Path $publicar $nombre

foreach ($f in 'Atarraya.exe', 'atarraya-puente.exe') {
    if (-not (Test-Path (Join-Path $programa $f))) { throw "Falta $f en ${programa}. Compila primero." }
}
& (Join-Path $raiz 'generar-avisos.ps1')

if (Test-Path $temporal) { Remove-Item -Recurse -Force $temporal }
New-Item -ItemType Directory -Force $temporal | Out-Null
Copy-Item (Join-Path $programa 'Atarraya.exe'), (Join-Path $programa 'atarraya-puente.exe') $temporal
& (Join-Path $raiz 'extension\armar.ps1') -Destino (Join-Path $temporal 'extension')
Copy-Item (Join-Path $raiz 'LICENSE') (Join-Path $temporal 'LICENCIA.txt')
Copy-Item (Join-Path $raiz 'AVISOS-DE-TERCEROS.txt') $temporal
$leeme = @"
Atarraya $version - descargas, torrents y enlaces magnet, gratis, de Brizna Soft
================================================================================

1. Abre Atarraya.exe (no hace falta instalar nada).
2. Si Windows dice "Windows protegió su PC": pulsa "Más información" y luego "Ejecutar de todas formas".
   Sale porque el programa es nuevo y todavía no tiene firma digital.
3. Para que el navegador le pase las descargas: en Atarraya pulsa "Navegadores" y sigue los pasos.
   La extensión para Chrome, Edge y Brave está en la carpeta extension\chrome.
4. Para bajar muchos enlaces de una vez usa el "Recolector": ahí ves el nombre y el tamaño de cada uno
   y eliges cuáles bajar. Nada empieza hasta que lo confirmas.

Los captchas de los sitios de descargas siempre los resuelves tú en tu navegador.
Ayuda, novedades y reportes de problemas: https://github.com/Brizna-Soft/atarraya
Sin publicidad, sin virus y sin recoger tus datos. Licencia MIT.
"@
[System.IO.File]::WriteAllText((Join-Path $temporal 'LEEME.txt'), $leeme, (New-Object System.Text.UTF8Encoding($true)))

$zip = Join-Path $publicar "$nombre.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $temporal '*') -DestinationPath $zip
Remove-Item -Recurse -Force $temporal
$huella = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
[System.IO.File]::WriteAllText("$zip.sha256", "$huella  $nombre.zip`n", (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Listo: $zip"
Write-Host "SHA256: $huella"
