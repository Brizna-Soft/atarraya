# Dibuja el ícono de Atarraya: cuadrado redondeado naranja con una flecha blanca hacia una bandeja.
# Genera res\app.ico (varios tamaños, PNG dentro) y los PNG de la extensión (16, 32, 48 y 128).
#
# Uso: powershell -ExecutionPolicy Bypass -File res\crear-icono.ps1
param(
    [string]$Raiz = (Split-Path $PSScriptRoot -Parent)
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$fondo = [System.Drawing.Color]::FromArgb(255, 0xE8, 0x59, 0x0C)
$blanco = [System.Drawing.Color]::White

function Redondeado([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function Dibujar([int]$lado) {
    $bmp = New-Object System.Drawing.Bitmap($lado, $lado, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $k = $lado / 100.0

    $margen = if ($lado -le 20) { 0 } else { 4 * $k }
    $caja = Redondeado $margen $margen ($lado - 2 * $margen) ($lado - 2 * $margen) (20 * $k)
    $g.FillPath((New-Object System.Drawing.SolidBrush($fondo)), $caja)

    $pincel = New-Object System.Drawing.SolidBrush($blanco)
    # Flecha: palo y punta
    $ancho = [Math]::Max(2.0, 14 * $k)
    $g.FillRectangle($pincel, [single](50 * $k - $ancho / 2), [single](20 * $k), [single]$ancho, [single](34 * $k))
    $punta = [System.Drawing.PointF[]]@(
        (New-Object System.Drawing.PointF([single](26 * $k), [single](48 * $k))),
        (New-Object System.Drawing.PointF([single](74 * $k), [single](48 * $k))),
        (New-Object System.Drawing.PointF([single](50 * $k), [single](72 * $k))))
    $g.FillPolygon($pincel, $punta)
    # Bandeja
    $grosor = [Math]::Max(2.0, 9 * $k)
    $g.FillRectangle($pincel, [single](22 * $k), [single](80 * $k - $grosor / 2), [single](56 * $k), [single]$grosor)

    $g.Dispose()
    return $bmp
}

function Png($bmp) {
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    return , $ms.ToArray()
}

# --- app.ico ---
$tamanos = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256
$imagenes = New-Object 'System.Collections.Generic.List[byte[]]'
foreach ($t in $tamanos) { $imagenes.Add((Png (Dibujar $t))) }
$salida = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter($salida)
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$tamanos.Count)
$desplazamiento = 6 + 16 * $tamanos.Count
for ($i = 0; $i -lt $tamanos.Count; $i++) {
    $t = $tamanos[$i]
    $lado = if ($t -ge 256) { 0 } else { $t }
    $w.Write([byte]$lado); $w.Write([byte]$lado); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$imagenes[$i].Length); $w.Write([uint32]$desplazamiento)
    $desplazamiento += $imagenes[$i].Length
}
foreach ($img in $imagenes) { $w.Write($img) }
$w.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $Raiz 'res\app.ico'), $salida.ToArray())

# --- íconos de la extensión ---
$carpeta = Join-Path $Raiz 'extension\comun\iconos'
New-Item -ItemType Directory -Force $carpeta | Out-Null
foreach ($t in 16, 32, 48, 128) {
    [System.IO.File]::WriteAllBytes((Join-Path $carpeta "icono-$t.png"), (Png (Dibujar $t)))
}
Write-Host 'Íconos listos.'
