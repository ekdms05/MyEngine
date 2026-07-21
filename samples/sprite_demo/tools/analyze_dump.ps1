# analyze_dump.ps1 - pixel verification for sprite_demo dumps (System.Drawing).
# Loads a BMP and reports:
#  - hero characteristic color pixel counts + centroid (skin, blue shirt)
#  - letterbox: bounding box of non-black content vs expected integer-scale dest rect
#  - intermediate-color check near hero edges (texel bleed test)
#  - imgui overlay panel pixel presence (top-left region grayish)
param(
    [Parameter(Mandatory=$true)][string]$Path
)
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap($Path)
$W = $bmp.Width; $H = $bmp.Height
Write-Host "== $Path ($W x $H) =="

# Lock bits for speed
$rect = New-Object System.Drawing.Rectangle(0,0,$W,$H)
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$bytes = New-Object byte[] ($stride * $H)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
$bmp.UnlockBits($data)

function Px([int]$x,[int]$y) {
    $o = $y*$stride + $x*4
    # BGRA
    return @($bytes[$o+2], $bytes[$o+1], $bytes[$o], $bytes[$o+3])  # R,G,B,A
}

# --- content bounding box (non near-black) ---
$minX=$W; $minY=$H; $maxX=-1; $maxY=-1
# hero blue shirt: R~60 G~120 B~220. skin: R~240 G~190 B~150.
$shirtN=0; $shirtSumX=0; $shirtSumY=0
$skinN=0;  $skinSumX=0;  $skinSumY=0
for ($y=0; $y -lt $H; $y++) {
    for ($x=0; $x -lt $W; $x++) {
        $p = Px $x $y
        $r=$p[0]; $g=$p[1]; $b=$p[2]
        if (($r+$g+$b) -gt 30) {
            if ($x -lt $minX){$minX=$x}; if ($x -gt $maxX){$maxX=$x}
            if ($y -lt $minY){$minY=$y}; if ($y -gt $maxY){$maxY=$y}
        }
        # blue shirt
        if ($b -gt 170 -and $r -lt 140 -and $g -gt 90 -and $g -lt 200) {
            $shirtN++; $shirtSumX+=$x; $shirtSumY+=$y
        }
        # skin
        if ($r -gt 200 -and $g -gt 150 -and $g -lt 220 -and $b -gt 110 -and $b -lt 190) {
            $skinN++; $skinSumX+=$x; $skinSumY+=$y
        }
    }
}
Write-Host ("content bbox: x[{0}..{1}] y[{2}..{3}]" -f $minX,$maxX,$minY,$maxY)
if ($shirtN -gt 0) {
    Write-Host ("shirt(blue) px={0} centroid=({1:N1},{2:N1})" -f $shirtN, ($shirtSumX/$shirtN), ($shirtSumY/$shirtN))
} else { Write-Host "shirt(blue) px=0 !!" }
if ($skinN -gt 0) {
    Write-Host ("skin px={0} centroid=({1:N1},{2:N1})" -f $skinN, ($skinSumX/$skinN), ($skinSumY/$skinN))
} else { Write-Host "skin px=0 !!" }

# --- distinct color count (texel-bleed proxy): point-sampled integer upscale should have
#     a small palette. Count distinct quantized(>>3) colors. High count => bleeding. ---
$set = @{}
for ($y=0; $y -lt $H; $y+=2) {
    for ($x=0; $x -lt $W; $x+=2) {
        $p = Px $x $y
        $key = "{0}_{1}_{2}" -f ([int]($p[0]/8)), ([int]($p[1]/8)), ([int]($p[2]/8))
        $set[$key] = 1
    }
}
Write-Host ("distinct quantized colors (sampled): {0}" -f $set.Count)

# --- imgui overlay: top-left panel (10,10)-(210,140). ImGui default dark panel ~ (37,37,38)+ text white. ---
$panelN=0; $textN=0
for ($y=12; $y -lt 140; $y++) {
    for ($x=12; $x -lt 210; $x++) {
        if ($x -ge $W -or $y -ge $H){continue}
        $p = Px $x $y
        $r=$p[0]; $g=$p[1]; $b=$p[2]
        # dark gray panel
        if ($r -gt 20 -and $r -lt 90 -and [math]::Abs($r-$g) -lt 20 -and [math]::Abs($g-$b) -lt 20) { $panelN++ }
        # bright text
        if ($r -gt 180 -and $g -gt 180 -and $b -gt 180) { $textN++ }
    }
}
Write-Host ("imgui panel px={0} text px={1}" -f $panelN, $textN)

$bmp.Dispose()
