# analyze2.ps1 - background-phase + hero + imgui + bleed analysis for a dump.
param([Parameter(Mandatory=$true)][string]$Path)
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap($Path)
$W=$bmp.Width;$H=$bmp.Height
$rect=New-Object System.Drawing.Rectangle(0,0,$W,$H)
$data=$bmp.LockBits($rect,[System.Drawing.Imaging.ImageLockMode]::ReadOnly,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride=$data.Stride
$bytes=New-Object byte[] ($stride*$H)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0,$bytes,0,$bytes.Length)
$bmp.UnlockBits($data)
function Px([int]$x,[int]$y){$o=$y*$stride+$x*4; return @($bytes[$o+2],$bytes[$o+1],$bytes[$o])}

Write-Host "== $Path ($W x $H) =="

# --- background phase: on scanline y=70 (above hero), find x of first A->B tile transition.
#     tileA sRGB ~ (112,118,134), tileB ~ (129,137,155). Detect transition x in 0..96.
$y=70
$firstTrans = -1
$prevIsB = $null
for($x=0;$x -lt 200;$x++){
    $p=Px $x $y
    # classify: B tile has higher brightness
    $bright = $p[0]+$p[1]+$p[2]
    $isB = ($bright -gt 400)   # A~364, B~421
    if($prevIsB -ne $null -and $isB -ne $prevIsB){ $firstTrans=$x; break }
    $prevIsB=$isB
}
Write-Host ("bg first tile transition x (y=70): {0}" -f $firstTrans)

# --- hero: blue shirt + skin (sRGB-adjusted thresholds) centroid + counts ---
$shirtN=0;$shirtSX=0;$shirtSY=0
$skinN=0;$skinSX=0;$skinSY=0
for($yy=0;$yy -lt $H;$yy++){
  for($xx=0;$xx -lt $W;$xx++){
    $p=Px $xx $yy; $r=$p[0];$g=$p[1];$b=$p[2]
    # blue shirt sRGB ~ (110-140, 170-200, 235-245): strong blue dominance
    if($b -gt 200 -and $b - $r -gt 70 -and $g -gt 150){ $shirtN++;$shirtSX+=$xx;$shirtSY+=$yy }
    # skin sRGB ~ (248,224,202): all high, r>g>b
    if($r -gt 235 -and $g -gt 205 -and $g -lt 240 -and $b -gt 180 -and $b -lt 225){ $skinN++;$skinSX+=$xx;$skinSY+=$yy }
  }
}
if($shirtN){Write-Host ("hero shirt px={0} centroid=({1:N1},{2:N1})" -f $shirtN,($shirtSX/$shirtN),($shirtSY/$shirtN))}else{Write-Host "hero shirt px=0 !!"}
if($skinN){Write-Host ("hero skin  px={0} centroid=({1:N1},{2:N1})" -f $skinN,($skinSX/$skinN),($skinSY/$skinN))}else{Write-Host "hero skin px=0 !!"}

# --- texel bleed test: scan a horizontal line through hero midriff, count distinct colors
#     across the hero span. Point-sample + integer scale => few flat runs, no gradient ramps.
$distinct=@{}
$hy=256
for($x=440;$x -lt 520;$x++){ $p=Px $x $hy; $distinct[("{0}_{1}_{2}" -f $p[0],$p[1],$p[2])]=1 }
Write-Host ("hero midline distinct exact colors (x440..520 y256): {0}" -f $distinct.Count)

# --- imgui overlay: top-left panel region. ImGui dark bg ~ (34..60 gray) + white text ---
$panelN=0;$textN=0
for($yy=12;$yy -lt 150;$yy++){
  for($xx=12;$xx -lt 220;$xx++){
    if($xx -ge $W -or $yy -ge $H){continue}
    $p=Px $xx $yy; $r=$p[0];$g=$p[1];$b=$p[2]
    if($r -gt 25 -and $r -lt 75 -and [math]::Abs($r-$g) -lt 14 -and [math]::Abs($g-$b) -lt 14){$panelN++}
    if($r -gt 190 -and $g -gt 190 -and $b -gt 190){$textN++}
  }
}
Write-Host ("imgui panel px={0} text px={1}" -f $panelN,$textN)
$bmp.Dispose()
