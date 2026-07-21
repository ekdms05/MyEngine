param([Parameter(Mandatory=$true)][string]$Path,[string]$Role='BRIDGE')
Add-Type -AssemblyName System.Drawing
$bmp=[System.Drawing.Bitmap]::FromFile((Resolve-Path $Path))
$W=$bmp.Width;$H=$bmp.Height
function Cls($r,$g,$b){
  if($r -lt 140 -and $g -gt 190 -and $b -gt 190){return 'A'}
  if($r -gt 190 -and $g -lt 150 -and $b -gt 190){return 'B'}
  if($r -gt 200 -and $g -gt 190 -and $b -lt 150){return 'P'}
  if($r -gt 210 -and $g -gt 210 -and $b -gt 210){return 'STATUE'}
  if($r -ge 180 -and $r -le 245 -and $g -ge 170 -and $g -le 225 -and $b -ge 120 -and $b -le 200 -and $g -ge $b){return 'BRIDGE'}
  if($g -gt $r -and $g -gt $b -and $g -gt 90){return 'GROUND'}
  return 'BG'
}
$minX=99999;$minY=99999;$maxX=-1;$maxY=-1;$n=0
for($y=0;$y -lt $H;$y++){for($x=0;$x -lt $W;$x++){
  $c=$bmp.GetPixel($x,$y)
  if((Cls $c.R $c.G $c.B) -eq $Role){$n++;if($x -lt $minX){$minX=$x};if($y -lt $minY){$minY=$y};if($x -gt $maxX){$maxX=$x};if($y -gt $maxY){$maxY=$y}}
}}
Write-Host "$Role : count=$n box=[$minX,$minY]-[$maxX,$maxY]"
$bmp.Dispose()
