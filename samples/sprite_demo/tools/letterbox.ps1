# letterbox.ps1 - verify integer-scale letterbox dest rect on a dump, excluding imgui overlay.
param([Parameter(Mandatory=$true)][string]$Path,[int]$IntW=960,[int]$IntH=540)
Add-Type -AssemblyName System.Drawing
$bmp=New-Object System.Drawing.Bitmap($Path)
$W=$bmp.Width;$H=$bmp.Height
$rect=New-Object System.Drawing.Rectangle(0,0,$W,$H)
$data=$bmp.LockBits($rect,[System.Drawing.Imaging.ImageLockMode]::ReadOnly,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride=$data.Stride
$bytes=New-Object byte[] ($stride*$H)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0,$bytes,0,$bytes.Length)
$bmp.UnlockBits($data)
function Bright([int]$x,[int]$y){$o=$y*$stride+$x*4; return [int]$bytes[$o]+[int]$bytes[$o+1]+[int]$bytes[$o+2]}

# expected letterbox for integer upscale
$scale=[math]::Min([math]::Floor($W/$IntW),[math]::Floor($H/$IntH)); if($scale -lt 1){$scale=1}
$dw=$IntW*$scale; $dh=$IntH*$scale
$ox=[math]::Floor(($W-$dw)/2); $oy=[math]::Floor(($H-$dh)/2)
Write-Host ("expected: scale={0}x dest=({1},{2},{3},{4})" -f $scale,$ox,$oy,$dw,$dh)

# Find content bbox but ignore top-left overlay box (0..260, 0..170)
$minX=$W;$minY=$H;$maxX=-1;$maxY=-1
for($y=0;$y -lt $H;$y++){
  for($x=0;$x -lt $W;$x++){
    if($x -lt 260 -and $y -lt 170){continue}   # skip imgui overlay area
    if((Bright $x $y) -gt 30){
      if($x -lt $minX){$minX=$x}; if($x -gt $maxX){$maxX=$x}
      if($y -lt $minY){$minY=$y}; if($y -gt $maxY){$maxY=$y}
    }
  }
}
Write-Host ("scene content bbox (excl overlay): x[{0}..{1}] y[{2}..{3}]" -f $minX,$maxX,$minY,$maxY)
$okL = ($minX -eq $ox); $okR = ($maxX -eq $ox+$dw-1); $okT=($minY -eq $oy); $okB=($maxY -eq $oy+$dh-1)
Write-Host ("letterbox match: left={0} right={1} top={2} bottom={3}" -f $okL,$okR,$okT,$okB)
# sample letterbox bars are black
$barOk = $true
if($oy -gt 0){ if((Bright ([int]($W/2)) ([int]($oy/2))) -gt 10){$barOk=$false} }
if($ox -gt 0){ if((Bright ([int]($ox/2)) ([int]($H/2))) -gt 10){$barOk=$false} }
Write-Host ("letterbox bars black: {0}" -f $barOk)
$bmp.Dispose()
