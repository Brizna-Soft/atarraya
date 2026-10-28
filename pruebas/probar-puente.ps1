# Habla con atarraya-puente.exe como lo haría el navegador (4 bytes de largo + JSON) y muestra la respuesta.
# Uso: probar-puente.ps1 -Puente <ruta a atarraya-puente.exe> -Json '{"tipo":"ping"}'
param(
    [Parameter(Mandatory = $true)][string]$Puente,
    [Parameter(Mandatory = $true)][string]$Json
)
$ErrorActionPreference = 'Stop'
$carpeta = Join-Path ([System.IO.Path]::GetTempPath()) ('puente-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $carpeta | Out-Null
$entrada = Join-Path $carpeta 'entrada.bin'
$salida = Join-Path $carpeta 'salida.bin'
$bytes = [System.Text.Encoding]::UTF8.GetBytes($Json)
$todo = [byte[]]([BitConverter]::GetBytes([uint32]$bytes.Length) + $bytes)
[System.IO.File]::WriteAllBytes($entrada, $todo)
cmd.exe /d /c "`"$Puente`" < `"$entrada`" > `"$salida`""
$r = [System.IO.File]::ReadAllBytes($salida)
if ($r.Length -ge 4) {
    $largo = [BitConverter]::ToUInt32($r, 0)
    'respuesta (' + $largo + ' bytes): ' + [System.Text.Encoding]::UTF8.GetString($r, 4, [Math]::Min($largo, $r.Length - 4))
} else { 'sin respuesta' }
Remove-Item -Recurse -Force $carpeta
