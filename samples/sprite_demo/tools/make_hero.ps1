# make_hero.ps1 - deterministic placeholder dot character PNG (24x32, transparent bg alpha0).
#
# Draws hero.png for the M1-B integration demo using System.Drawing, deterministically.
# Recognizable character: skin-tone head + dark outline + eyes, blue shirt body,
# brown legs, dark boots. Pixel-art: no anti-aliasing, pixel-by-pixel fills.
#
# Usage: powershell -ExecutionPolicy Bypass -File make_hero.ps1 [-Out <path>]
param(
    [string]$Out = (Join-Path $PSScriptRoot "..\assets\hero.png")
)

Add-Type -AssemblyName System.Drawing

$W = 24
$H = 32
$bmp = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

# init all transparent (alpha 0)
for ($y = 0; $y -lt $H; $y++) {
    for ($x = 0; $x -lt $W; $x++) {
        $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(0, 0, 0, 0))
    }
}

# palette (clearly distinct)
$outline = [System.Drawing.Color]::FromArgb(255, 20, 20, 28)
$skin    = [System.Drawing.Color]::FromArgb(255, 240, 190, 150)
$eye     = [System.Drawing.Color]::FromArgb(255, 30, 30, 40)
$shirt   = [System.Drawing.Color]::FromArgb(255, 60, 120, 220)
$shirtLt = [System.Drawing.Color]::FromArgb(255, 90, 150, 240)
$pants   = [System.Drawing.Color]::FromArgb(255, 120, 80, 50)
$boot    = [System.Drawing.Color]::FromArgb(255, 40, 30, 25)

function Fill([int]$x0, [int]$y0, [int]$x1, [int]$y1, $col) {
    for ($y = $y0; $y -le $y1; $y++) {
        for ($x = $x0; $x -le $x1; $x++) {
            if ($x -ge 0 -and $x -lt $W -and $y -ge 0 -and $y -lt $H) {
                $bmp.SetPixel($x, $y, $col)
            }
        }
    }
}

# head (y 1..12), center x 8..15
Fill 8 1 15 12 $outline
Fill 9 2 14 11 $skin
Fill 10 6 10 7 $eye
Fill 13 6 13 7 $eye

# body / shirt (y 12..22)
Fill 6 12 17 22 $outline
Fill 7 13 16 21 $shirt
Fill 7 13 16 14 $shirtLt
Fill 7 15 8 20 $shirtLt
Fill 15 15 16 20 $shirtLt

# legs / pants (y 22..29)
Fill 8 22 15 29 $outline
Fill 9 23 11 28 $pants
Fill 13 23 15 28 $pants

# feet / boots (y 29..31)
Fill 8 29 11 31 $boot
Fill 13 29 16 31 $boot

$outDir = Split-Path -Parent $Out
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "wrote $Out ($W x $H RGBA PNG)"
