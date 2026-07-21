# analyze.ps1 — pixel verification of bridge_demo scenario dumps (System.Drawing).
#
# Classifies pixels by role color and reports counts / bounding boxes / overlap occlusion.
# Roles (from make_assets.ps1):
#   A(cyan)   R<120 G>150 B>150      : character A (on bridge)
#   B(magenta)R>150 G<120 B>150      : character B (under bridge)
#   P(yellow) R>180 G>160 B<120      : player
#   bridge(tan-gray) R 150-210 G 120-180 B 80-140 (and G approx > B) : bridge deck
#   ground(green) G>R and G>B and G>70                              : ground
#   statue(white/gray, lit) R,G,B all >180 and near-equal          : statue cube
#
# Usage: powershell -File analyze.ps1 -Path dump.bmp [-Role histogram]
param(
    [Parameter(Mandatory=$true)][string]$Path,
    [switch]$Histogram
)

Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Path))
$W = $bmp.Width; $H = $bmp.Height

function Classify($r,$g,$b) {
    # order matters — most specific first
    if ($r -lt 120 -and $g -gt 150 -and $b -gt 150) { return 'A' }        # cyan
    if ($r -gt 150 -and $g -lt 120 -and $b -gt 150) { return 'B' }        # magenta
    if ($r -gt 180 -and $g -gt 150 -and $b -lt 120) { return 'P' }        # yellow
    if ($r -gt 175 -and $g -gt 175 -and $b -gt 175 -and [math]::Abs($r-$g) -lt 40 -and [math]::Abs($g-$b) -lt 40) { return 'STATUE' }
    if ($g -gt $r -and $g -gt $b -and $g -gt 70 -and $r -lt 120) { return 'GROUND' }
    if ($r -ge 130 -and $r -le 215 -and $g -ge 110 -and $g -le 190 -and $b -ge 70 -and $b -le 150 -and $g -ge $b) { return 'BRIDGE' }
    return 'BG'
}

$counts = @{}
$bbox = @{}
foreach ($role in 'A','B','P','STATUE','GROUND','BRIDGE','BG') {
    $counts[$role] = 0
    $bbox[$role] = @{ minX=99999; minY=99999; maxX=-1; maxY=-1 }
}

# Downsample stride for speed but full for accuracy where needed
for ($y=0; $y -lt $H; $y++) {
    for ($x=0; $x -lt $W; $x++) {
        $c = $bmp.GetPixel($x,$y)
        $role = Classify $c.R $c.G $c.B
        $counts[$role]++
        $bb = $bbox[$role]
        if ($x -lt $bb.minX) { $bb.minX = $x }
        if ($y -lt $bb.minY) { $bb.minY = $y }
        if ($x -gt $bb.maxX) { $bb.maxX = $x }
        if ($y -gt $bb.maxY) { $bb.maxY = $y }
    }
}

Write-Host "=== $Path ($W x $H) ==="
foreach ($role in 'A','B','P','STATUE','GROUND','BRIDGE','BG') {
    $bb = $bbox[$role]
    $box = if ($bb.maxX -ge 0) { "[$($bb.minX),$($bb.minY)]-[$($bb.maxX),$($bb.maxY)]" } else { "(none)" }
    Write-Host ("{0,-8} count={1,-7} box={2}" -f $role, $counts[$role], $box)
}
$bmp.Dispose()
