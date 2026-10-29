# Compila Atarraya y deja el programa listo en .\programa
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File compilar.ps1 [-Limpiar] [-Publicacion] [-SinTorrents] [-SoloPreparar]
#
#   -Limpiar       borra la carpeta build antes de compilar
#   -Publicacion   versión limpia (la de version.txt, sin "-local"); la usa GitHub Actions
#   -SinTorrents   sin libtorrent (ni Boost ni OpenSSL): compila mucho más rápido, para probar lo demás
#   -SoloPreparar  solo prepara las bibliotecas con vcpkg, sin compilar el programa (lo usa GitHub Actions)
#
# Necesita Visual Studio 2022 (o sus Build Tools) con C++ y Git. Si la variable VCPKG_ROOT no está puesta,
# baja vcpkg (versión fija) a vendor\vcpkg la primera vez.
param(
    [switch]$Limpiar,
    [switch]$Publicacion,
    [switch]$SinTorrents,
    [switch]$SoloPreparar
)

$ErrorActionPreference = 'Stop'
$codigo = $PSScriptRoot
$build = Join-Path $codigo 'build'
$programa = Join-Path $codigo 'programa'
$vcpkgEtiqueta = '2026.10.16'

function Paso($texto) { Write-Host "`n=== $texto ===" -ForegroundColor Cyan }

Paso 'Buscando Visual Studio 2022'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'No encuentro Visual Studio 2022 (falta vswhere.exe)' }
$vs = & $vswhere -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -latest -property installationPath
if (-not $vs) { throw 'No encuentro Visual Studio 2022 con las herramientas de C++' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { throw "No encuentro el CMake de Visual Studio en $cmake" }

Paso 'Preparando vcpkg'
if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = Join-Path $codigo 'vendor\vcpkg'
    if (-not (Test-Path (Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'))) {
        & git clone --depth 1 --branch $vcpkgEtiqueta https://github.com/microsoft/vcpkg $env:VCPKG_ROOT
        if ($LASTEXITCODE -ne 0) { throw 'No pude bajar vcpkg' }
    }
}
if (-not (Test-Path (Join-Path $env:VCPKG_ROOT 'vcpkg.exe'))) {
    & (Join-Path $env:VCPKG_ROOT 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'No pude preparar vcpkg' }
}
$env:VCPKG_DISABLE_METRICS = '1'
Write-Host "vcpkg: $env:VCPKG_ROOT"

$version = (Get-Content (Join-Path $codigo 'version.txt') -TotalCount 1).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "version.txt debe tener el formato 1.2.3 (tiene '$version')" }
$texto = $version
if (-not $Publicacion) {
    $sha = & git -C $codigo rev-parse --short=7 HEAD 2>$null
    $texto = if ($LASTEXITCODE -eq 0 -and $sha) { "$version-local+$($sha.Trim())" } else { "$version-local" }
}
if ($SinTorrents) { $texto += '-sin-torrents' }
Write-Host "Versión: $texto"

if ($Limpiar -and (Test-Path $build)) {
    Paso 'Borrando la compilación anterior'
    Remove-Item -Recurse -Force $build
}

$conTorrents = if ($SinTorrents) { 'OFF' } else { 'ON' }
Paso 'Configurando (la primera vez vcpkg compila wxWidgets, curl y, con torrents, Boost, OpenSSL y libtorrent; tarda)'
& $cmake -S $codigo -B $build -G 'Visual Studio 17 2022' -A x64 -T v143 `
    "-DAPP_VERSION_TEXTO=$texto" "-DCON_TORRENTS=$conTorrents" `
    "-DVCPKG_INSTALL_OPTIONS=--clean-buildtrees-after-build;--clean-packages-after-build"
if ($LASTEXITCODE -ne 0) { throw "Falló la configuración (código $LASTEXITCODE)" }
if ($SoloPreparar) {
    Write-Host 'Bibliotecas listas.' -ForegroundColor Green
    return
}

Paso 'Compilando'
& $cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Falló la compilación (código $LASTEXITCODE)" }

Paso 'Copiando el programa'
New-Item -ItemType Directory -Force $programa | Out-Null
Copy-Item (Join-Path $build 'Release\Atarraya.exe') $programa -Force
Copy-Item (Join-Path $build 'Release\atarraya-puente.exe') $programa -Force
$destinoExt = Join-Path $programa 'extension'
if (Test-Path $destinoExt) { Remove-Item -Recurse -Force $destinoExt }
& (Join-Path $codigo 'extension\armar.ps1') -Destino $destinoExt
Write-Host "Listo: $programa" -ForegroundColor Green
