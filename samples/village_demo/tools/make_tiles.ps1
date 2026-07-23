# make_tiles.ps1 — deterministic Tales-Weaver-style village tileset (48px tiles).
#
# Produces one PNG per tile role so HybridRenderer's per-entity TilemapRenderer can bind a
# distinct tileset texture per layer, and a pixel-verification pass can classify a tile by its
# dominant color. All tiles are 48x48 opaque (PPU48, one tile == one world unit).
#
# Roles (dominant color in parentheses — used by verify passes):
#   tile_grass.png   ground/grass         (deep green)     — the walkable field
#   tile_path.png    dirt road            (tan/brown)      — the village main road
#   tile_water.png   stream water         (blue)           — the creek (impassable)
#   tile_bridge.png  bridge deck (top)    (light wood)     — one-way bridge over the creek
#   tile_slope.png   hill slope ramp      (olive)          — the gentle hill (depth ramp)
#   tile_plaza.png   stone plaza          (gray)           — around the fountain/statue
#
# Deterministic: fixed value-noise via a hash of (x,y) so texture detail is reproducible byte-for-byte.
# Usage: powershell -ExecutionPolicy Bypass -File make_tiles.ps1
param(
    [string]$OutDir = (Join-Path $PSScriptRoot "..\assets")
)

Add-Type -AssemblyName System.Drawing
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

$TILE = 48

# Deterministic per-pixel hash noise in [-1,1] (no RNG state → reproducible).
# All arithmetic is masked to 32 bits (unchecked wrap) to avoid Int64 overflow exceptions.
function Noise2([int]$x, [int]$y, [int]$seed) {
    # Keep every operand under ~2^16 before multiplying so products stay well within Int64.
    $mask = [int64]0xFFFF
    $h = ([int64]$x * 374761 + [int64]$y * 668265 + [int64]$seed * 36243) -band 0x7fffffff
    $h = (($h -bxor ($h -shr 13)) -band $mask) * 40503   # <=65535*40503 < 2^32
    $h = $h -band 0x7fffffff
    return (($h % 2000) / 1000.0) - 1.0
}

function Clamp255([double]$v) {
    if ($v -lt 0) { return 0 } elseif ($v -gt 255) { return 255 } else { return [int][math]::Round($v) }
}

# Generate a tile: base color + noise dither + optional edge line + optional feature callback.
function New-Tile {
    param(
        [string]$Path, [int]$R, [int]$G, [int]$B,
        [double]$NoiseAmp = 10.0, [int]$Seed = 1,
        [bool]$EdgeLine = $true,
        [scriptblock]$Feature = $null
    )
    $bmp = New-Object System.Drawing.Bitmap($TILE, $TILE, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $TILE; $y++) {
        for ($x = 0; $x -lt $TILE; $x++) {
            $n = (Noise2 $x $y $Seed) * $NoiseAmp
            $cr = Clamp255 ($R + $n); $cg = Clamp255 ($G + $n); $cb = Clamp255 ($B + $n)
            if ($EdgeLine -and ($x -eq 0 -or $y -eq 0)) {
                # darker grid edge for tiling visibility (keeps role hue)
                $cr = Clamp255 ($R * 0.62); $cg = Clamp255 ($G * 0.62); $cb = Clamp255 ($B * 0.62)
            }
            $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, $cr, $cg, $cb))
        }
    }
    if ($Feature) { & $Feature $bmp }
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "wrote $Path ($TILE x $TILE)"
}

# --- grass: deep green field, speckled with a few lighter blades ---
$grassFeature = {
    param($b)
    for ($i = 0; $i -lt 14; $i++) {
        $gx = ((Noise2 $i 7 5) * 0.5 + 0.5) * ($TILE - 4) + 2
        $gy = ((Noise2 $i 11 6) * 0.5 + 0.5) * ($TILE - 6) + 3
        $gx = [int]$gx; $gy = [int]$gy
        $blade = [System.Drawing.Color]::FromArgb(255, 70, 150, 70)
        $b.SetPixel($gx, $gy, $blade)
        if ($gy - 1 -ge 0) { $b.SetPixel($gx, $gy - 1, $blade) }
    }
}
New-Tile -Path (Join-Path $OutDir "tile_grass.png")  -R 46 -G 120 -B 58  -NoiseAmp 12 -Seed 3 -Feature $grassFeature

# --- path: packed dirt road, tan with pebbles ---
$pathFeature = {
    param($b)
    for ($i = 0; $i -lt 10; $i++) {
        $px = [int](((Noise2 $i 3 9) * 0.5 + 0.5) * ($TILE - 4) + 2)
        $py = [int](((Noise2 $i 5 10) * 0.5 + 0.5) * ($TILE - 4) + 2)
        $peb = [System.Drawing.Color]::FromArgb(255, 150, 120, 82)
        $b.SetPixel($px, $py, $peb)
    }
}
New-Tile -Path (Join-Path $OutDir "tile_path.png")   -R 176 -G 142 -B 96 -NoiseAmp 10 -Seed 4 -Feature $pathFeature

# --- water: blue stream with horizontal ripple bands ---
$waterFeature = {
    param($b)
    for ($y = 4; $y -lt $TILE; $y += 8) {
        for ($x = 1; $x -lt $TILE; $x++) {
            $b.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, 120, 180, 235))
        }
    }
}
New-Tile -Path (Join-Path $OutDir "tile_water.png")  -R 48 -G 108 -B 200 -NoiseAmp 14 -Seed 7 -Feature $waterFeature

# --- bridge: light wooden deck with plank seams (one-way top) ---
$bridgeFeature = {
    param($b)
    $seam = [System.Drawing.Color]::FromArgb(255, 120, 92, 56)
    for ($x = 0; $x -lt $TILE; $x += 12) {
        for ($y = 0; $y -lt $TILE; $y++) { $b.SetPixel($x, $y, $seam) }
    }
}
New-Tile -Path (Join-Path $OutDir "tile_bridge.png") -R 196 -G 158 -B 110 -NoiseAmp 8 -Seed 11 -EdgeLine $false -Feature $bridgeFeature

# --- slope: olive hill ramp (depth ramp) ---
New-Tile -Path (Join-Path $OutDir "tile_slope.png")  -R 132 -G 146 -B 74 -NoiseAmp 10 -Seed 13

# --- plaza: stone tiles around the fountain (gray, block grid) ---
$plazaFeature = {
    param($b)
    $grout = [System.Drawing.Color]::FromArgb(255, 96, 96, 104)
    for ($k = 0; $k -lt $TILE; $k += 16) {
        for ($i = 0; $i -lt $TILE; $i++) { $b.SetPixel($k, $i, $grout); $b.SetPixel($i, $k, $grout) }
    }
}
New-Tile -Path (Join-Path $OutDir "tile_plaza.png")  -R 158 -G 158 -B 166 -NoiseAmp 8 -Seed 17 -Feature $plazaFeature

# --- white.png : 1x1 white (mesh albedo tint base) ---
$w = New-Object System.Drawing.Bitmap(1, 1, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$w.SetPixel(0, 0, [System.Drawing.Color]::FromArgb(255, 255, 255, 255))
$w.Save((Join-Path $OutDir "white.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$w.Dispose()
Write-Host "wrote white.png"
Write-Host "DONE tiles"
