# make_chars.ps1 — deterministic 8-direction character sheets for the village demo.
#
# Layout matches SpriteSheetImporter::SliceGrid (row-major): row = Dir8 (0..7), col = walk frame
# (0..3). Cell = 24x32. Foot pivot = (12,32). Contact frames (col 1 & 3) shift the legs so the
# walk animation reads, and the integration agent attaches "footstep" AnimEventMarkers at slots 1,3.
#
# Dir8 row order (engine convention, see character_demo):
#   0 down  1 down_left  2 left  3 up_left  4 up  5 up_right  6 right  7 down_right
#
# Sheets (distinct body color so a pixel dump identifies who is on screen):
#   player_sheet.png   player            bright BLUE
#   npc_chief.png      village chief     PURPLE   (robe)
#   npc_merchant.png   traveling merchant ORANGE  (apron)
#   npc_guard.png      gate guard        STEEL/CYAN (armor)
#
# Usage: powershell -ExecutionPolicy Bypass -File make_chars.ps1
param(
    [string]$OutDir = (Join-Path $PSScriptRoot "..\assets")
)

Add-Type -AssemblyName System.Drawing
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

$CELL_W = 24; $CELL_H = 32; $COLS = 4; $ROWS = 8

$script:CW = $CELL_W; $script:CH = $CELL_H

function New-CharSheet {
    param([string]$Path, [int]$BR, [int]$BG, [int]$BB, [int]$HatR = -1, [int]$HatG = 0, [int]$HatB = 0)

    $sheetW = $CELL_W * $COLS
    $sheetH = $CELL_H * $ROWS
    $bmp = New-Object System.Drawing.Bitmap($sheetW, $sheetH, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $outline = [System.Drawing.Color]::FromArgb(255, 15, 15, 20)
    $skin    = [System.Drawing.Color]::FromArgb(255, 240, 200, 160)
    $clear   = [System.Drawing.Color]::FromArgb(0, 0, 0, 0)
    $body    = [System.Drawing.Color]::FromArgb(255, $BR, $BG, $BB)
    $hasHat  = ($HatR -ge 0)
    if ($hasHat) { $hat = [System.Drawing.Color]::FromArgb(255, $HatR, $HatG, $HatB) }

    for ($y = 0; $y -lt $sheetH; $y++) { for ($x = 0; $x -lt $sheetW; $x++) { $bmp.SetPixel($x, $y, $clear) } }

    $ox = 0; $oy = 0
    function Fill($x0, $y0, $x1, $y1, $col) {
        for ($y = $y0; $y -le $y1; $y++) {
            for ($x = $x0; $x -le $x1; $x++) {
                if ($x -ge 0 -and $x -lt $script:CW -and $y -ge 0 -and $y -lt $script:CH) {
                    $bmp.SetPixel($script:ox + $x, $script:oy + $y, $col)
                }
            }
        }
    }

    for ($row = 0; $row -lt $ROWS; $row++) {
        for ($col = 0; $col -lt $COLS; $col++) {
            $script:ox = $col * $CELL_W
            $script:oy = $row * $CELL_H
            # head
            Fill 8 1 15 12 $outline
            Fill 9 2 14 11 $skin
            # hat/hair band (role accent) on top of head
            if ($hasHat) { Fill 8 1 15 3 $hat }
            # eyes shift per facing so a dump differs per direction even at rest
            $eyeShift = [int]($row % 3) - 1
            Fill (10 + $eyeShift) 6 (10 + $eyeShift) 7 $outline
            Fill (13 + $eyeShift) 6 (13 + $eyeShift) 7 $outline
            # body (role color)
            Fill 6 12 17 24 $outline
            Fill 7 13 16 23 $body
            # legs — contact pose alternates on frames 1 and 3 (footstep frames)
            if ($col -eq 1) {
                Fill 7  24 10 31 $outline
                Fill 13 24 16 31 $outline
                Fill 8  25 9  30 $body
                Fill 14 25 15 30 $body
            } elseif ($col -eq 3) {
                Fill 9  24 12 31 $outline
                Fill 12 24 15 31 $outline
                Fill 10 25 11 30 $body
                Fill 13 25 14 30 $body
            } else {
                Fill 8 24 15 31 $outline
                Fill 9 25 14 30 $body
            }
        }
    }
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "wrote $Path ($sheetW x $sheetH, ${COLS}x${ROWS} cells of ${CELL_W}x${CELL_H})"
}

New-CharSheet -Path (Join-Path $OutDir "player_sheet.png") -BR 70  -BG 120 -BB 240                       # player: blue
New-CharSheet -Path (Join-Path $OutDir "npc_chief.png")    -BR 150 -BG 70  -BB 200 -HatR 90  -HatG 40 -HatB 130  # chief: purple robe + dark hat
New-CharSheet -Path (Join-Path $OutDir "npc_merchant.png") -BR 235 -BG 140 -BB 50  -HatR 200 -HatG 90 -HatB 20   # merchant: orange
New-CharSheet -Path (Join-Path $OutDir "npc_guard.png")    -BR 90  -BG 170 -BB 190 -HatR 120 -HatG 130 -HatB 140 # guard: steel/cyan
Write-Host "DONE chars"
