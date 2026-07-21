# make_assets.ps1 — deterministic test assets for bridge_demo (M2-C hybrid depth demo).
#
# Produces distinctly-colored PNGs so the pixel-verification pass can classify roles by color:
#   hero_A.png    — character A (walks on bridge)      : bright CYAN body
#   hero_B.png    — character B (passes under bridge)   : bright MAGENTA body
#   hero_P.png    — player-controlled character         : bright YELLOW body
#   tile_ground.png — ground tiles                      : dark GREEN
#   tile_bridge.png — bridge deck (one-way top)         : light GRAY-BROWN
#   tile_slope.png  — slope ramp tiles                  : TAN
#   white.png       — 1x1 white (statue albedo tint base)
#
# Pixel-art: no anti-aliasing, opaque bodies on transparent bg (characters) or opaque tiles.
# Usage: powershell -ExecutionPolicy Bypass -File make_assets.ps1
param(
    [string]$OutDir = (Join-Path $PSScriptRoot "..\assets")
)

Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

function New-Char([string]$path, $bodyCol) {
    $W = 24; $H = 32
    $bmp = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $outline = [System.Drawing.Color]::FromArgb(255, 15, 15, 20)
    for ($y = 0; $y -lt $H; $y++) { for ($x = 0; $x -lt $W; $x++) { $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(0,0,0,0)) } }
    function Fill($x0,$y0,$x1,$y1,$col) {
        for ($y=$y0; $y -le $y1; $y++){ for ($x=$x0; $x -le $x1; $x++){ if($x -ge 0 -and $x -lt 24 -and $y -ge 0 -and $y -lt 32){ $bmp.SetPixel($x,$y,$col) } } }
    }
    # head
    Fill 8 1 15 12 $outline
    Fill 9 2 14 11 ([System.Drawing.Color]::FromArgb(255,240,200,160))
    Fill 10 6 10 7 $outline
    Fill 13 6 13 7 $outline
    # body (role color)
    Fill 6 12 17 24 $outline
    Fill 7 13 16 23 $bodyCol
    # legs
    Fill 8 24 15 31 $outline
    Fill 9 25 14 30 $bodyCol
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "wrote $path"
}

function New-Tile([string]$path, $col) {
    $W = 48; $H = 48
    $bmp = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y=0; $y -lt $H; $y++){ for ($x=0; $x -lt $W; $x++){
        # subtle grid lines to see tiling; keep dominant color for classification
        $edge = ($x -eq 0 -or $y -eq 0)
        if ($edge) { $bmp.SetPixel($x,$y,[System.Drawing.Color]::FromArgb(255,[int]($col.R*0.65),[int]($col.G*0.65),[int]($col.B*0.65))) }
        else { $bmp.SetPixel($x,$y,$col) }
    } }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "wrote $path"
}

New-Char (Join-Path $OutDir "hero_A.png") ([System.Drawing.Color]::FromArgb(255, 30, 220, 230))   # cyan
New-Char (Join-Path $OutDir "hero_B.png") ([System.Drawing.Color]::FromArgb(255, 230, 40, 210))   # magenta
New-Char (Join-Path $OutDir "hero_P.png") ([System.Drawing.Color]::FromArgb(255, 245, 220, 30))   # yellow

New-Tile (Join-Path $OutDir "tile_ground.png") ([System.Drawing.Color]::FromArgb(255, 40, 110, 55))   # green
New-Tile (Join-Path $OutDir "tile_bridge.png") ([System.Drawing.Color]::FromArgb(255, 180, 150, 110)) # tan-gray bridge deck
New-Tile (Join-Path $OutDir "tile_slope.png")  ([System.Drawing.Color]::FromArgb(255, 200, 175, 90))  # tan slope

# 1x1 white for statue albedo
$w = New-Object System.Drawing.Bitmap(1,1,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$w.SetPixel(0,0,[System.Drawing.Color]::FromArgb(255,255,255,255))
$w.Save((Join-Path $OutDir "white.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$w.Dispose()
Write-Host "wrote white.png"
Write-Host "DONE"
