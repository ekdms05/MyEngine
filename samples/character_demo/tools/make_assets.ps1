# make_assets.ps1 — deterministic M3-C character_demo assets (8-dir sheet + footstep WAV + grid).
#
# Produces:
#   player_sheet.png  — 8 rows (Dir8: down,down_left,left,up_left,up,up_right,right,down_right)
#                       x 4 cols (walk frames). Cell = 24x32. Body tint varies per direction so a
#                       pixel dump differs per facing; frame 1 & 3 shift the legs (footstep contact).
#   footstep.wav      — short mono S16 percussive click (decaying noise burst), 44100 Hz.
#   tile_grid.png     — 48x48 background grid tile.
#   white.png         — 1x1 white.
#
# Deterministic: fixed RNG seed for the footstep noise so offline mixer output is reproducible.
# Usage: powershell -ExecutionPolicy Bypass -File make_assets.ps1
param(
    [string]$OutDir = (Join-Path $PSScriptRoot "..\assets")
)

Add-Type -AssemblyName System.Drawing
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

$CELL_W = 24; $CELL_H = 32; $COLS = 4; $ROWS = 8

# Per-direction body colors (distinct so a facing change is visible in a pixel dump).
$dirColors = @(
    (,@(240, 60, 60)),    # 0 down      red
    (,@(240,150, 40)),    # 1 down_left orange
    (,@(240,230, 40)),    # 2 left      yellow
    (,@(120,230, 40)),    # 3 up_left   lime
    (,@( 40,220,120)),    # 4 up        green
    (,@( 40,210,230)),    # 5 up_right  cyan
    (,@( 80,120,240)),    # 6 right     blue
    (,@(210, 70,230))     # 7 down_right magenta
)

$sheetW = $CELL_W * $COLS
$sheetH = $CELL_H * $ROWS
$bmp = New-Object System.Drawing.Bitmap($sheetW, $sheetH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$outline = [System.Drawing.Color]::FromArgb(255, 15, 15, 20)
$skin    = [System.Drawing.Color]::FromArgb(255, 240, 200, 160)
$clear   = [System.Drawing.Color]::FromArgb(0, 0, 0, 0)

for ($y = 0; $y -lt $sheetH; $y++) { for ($x = 0; $x -lt $sheetW; $x++) { $bmp.SetPixel($x, $y, $clear) } }

function FillCell($ox, $oy, $x0, $y0, $x1, $y1, $col) {
    for ($y = $y0; $y -le $y1; $y++) {
        for ($x = $x0; $x -le $x1; $x++) {
            if ($x -ge 0 -and $x -lt $script:CELL_W -and $y -ge 0 -and $y -lt $script:CELL_H) {
                $bmp.SetPixel($ox + $x, $oy + $y, $col)
            }
        }
    }
}

for ($row = 0; $row -lt $ROWS; $row++) {
    $rgb = $dirColors[$row][0]
    $body = [System.Drawing.Color]::FromArgb(255, [int]$rgb[0], [int]$rgb[1], [int]$rgb[2])
    for ($col = 0; $col -lt $COLS; $col++) {
        $ox = $col * $CELL_W
        $oy = $row * $CELL_H
        # head
        FillCell $ox $oy 8 1 15 12 $outline
        FillCell $ox $oy 9 2 14 11 $skin
        # eyes shift slightly by facing (row) so a dump differs per direction even at rest frame
        $eyeShift = [int]($row % 3) - 1
        FillCell $ox $oy (10 + $eyeShift) 6 (10 + $eyeShift) 7 $outline
        FillCell $ox $oy (13 + $eyeShift) 6 (13 + $eyeShift) 7 $outline
        # body (role color)
        FillCell $ox $oy 6 12 17 24 $outline
        FillCell $ox $oy 7 13 16 23 $body
        # legs — contact pose alternates on frame 1 and 3 (footstep frames)
        if ($col -eq 1) {
            FillCell $ox $oy 7  24 10 31 $outline
            FillCell $ox $oy 13 24 16 31 $outline
            FillCell $ox $oy 8  25 9  30 $body
            FillCell $ox $oy 14 25 15 30 $body
        } elseif ($col -eq 3) {
            FillCell $ox $oy 9  24 12 31 $outline
            FillCell $ox $oy 12 24 15 31 $outline
            FillCell $ox $oy 10 25 11 30 $body
            FillCell $ox $oy 13 25 14 30 $body
        } else {
            FillCell $ox $oy 8 24 15 31 $outline
            FillCell $ox $oy 9 25 14 30 $body
        }
    }
}
$sheetPath = Join-Path $OutDir "player_sheet.png"
$bmp.Save($sheetPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "wrote $sheetPath ($sheetW x $sheetH, ${COLS}x${ROWS} cells of ${CELL_W}x${CELL_H})"

# ---- footstep.wav : mono S16, 44100 Hz, ~90ms decaying noise burst (deterministic) ----
$sr = 44100
$durSec = 0.09
$nFrames = [int]($sr * $durSec)
$rng = New-Object System.Random(1337)   # fixed seed → reproducible offline mixer output
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
# RIFF header
$dataBytes = $nFrames * 2
[void]$bw.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
$bw.Write([int](36 + $dataBytes))
[void]$bw.Write([System.Text.Encoding]::ASCII.GetBytes("WAVE"))
[void]$bw.Write([System.Text.Encoding]::ASCII.GetBytes("fmt "))
$bw.Write([int]16)          # fmt chunk size
$bw.Write([int16]1)         # PCM
$bw.Write([int16]1)         # mono
$bw.Write([int]$sr)         # sample rate
$bw.Write([int]($sr * 2))   # byte rate
$bw.Write([int16]2)         # block align
$bw.Write([int16]16)        # bits per sample
[void]$bw.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
$bw.Write([int]$dataBytes)
for ($i = 0; $i -lt $nFrames; $i++) {
    $t = $i / $nFrames
    $env = [math]::Exp(-6.0 * $t)                    # fast decay
    $noise = ($rng.NextDouble() * 2.0 - 1.0)
    $tone = [math]::Sin(2 * [math]::PI * 140.0 * $i / $sr)  # low thud
    $s = ($noise * 0.5 + $tone * 0.5) * $env * 0.8
    $v = [int]([math]::Round($s * 30000))
    if ($v -gt 32767) { $v = 32767 }
    if ($v -lt -32768) { $v = -32768 }
    $bw.Write([int16]$v)
}
$bw.Flush()
$wavPath = Join-Path $OutDir "footstep.wav"
[System.IO.File]::WriteAllBytes($wavPath, $ms.ToArray())
$bw.Dispose(); $ms.Dispose()
Write-Host "wrote $wavPath ($nFrames frames)"

# ---- tile_grid.png : 48x48 background grid ----
$g = New-Object System.Drawing.Bitmap(48, 48, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$base = [System.Drawing.Color]::FromArgb(255, 28, 32, 44)
$line = [System.Drawing.Color]::FromArgb(255, 52, 60, 80)
for ($y = 0; $y -lt 48; $y++) { for ($x = 0; $x -lt 48; $x++) {
    if ($x -eq 0 -or $y -eq 0) { $g.SetPixel($x, $y, $line) } else { $g.SetPixel($x, $y, $base) }
} }
$gridPath = Join-Path $OutDir "tile_grid.png"
$g.Save($gridPath, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
Write-Host "wrote $gridPath"

# ---- white.png ----
$w = New-Object System.Drawing.Bitmap(1, 1, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$w.SetPixel(0, 0, [System.Drawing.Color]::FromArgb(255, 255, 255, 255))
$w.Save((Join-Path $OutDir "white.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$w.Dispose()
Write-Host "wrote white.png"
Write-Host "DONE"
