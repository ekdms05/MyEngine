param([Parameter(Mandatory=$true)][string]$Path)
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Path))
$W=$bmp.Width; $H=$bmp.Height
# Sample a grid of points and print unique-ish colors with counts
$hist = @{}
for ($y=0; $y -lt $H; $y+=6) {
  for ($x=0; $x -lt $W; $x+=6) {
    $c=$bmp.GetPixel($x,$y)
    $key = "{0},{1},{2}" -f ([int]($c.R/16)*16),([int]($c.G/16)*16),([int]($c.B/16)*16)
    if ($hist.ContainsKey($key)) { $hist[$key]++ } else { $hist[$key]=1 }
  }
}
Write-Host "=== color histogram (quantized/16) $Path ==="
$hist.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 20 | ForEach-Object {
  Write-Host ("{0,-14} {1}" -f $_.Key, $_.Value)
}
# Center row probe
Write-Host "--- center pixels (y=270) x=440..520 step8 ---"
for ($x=440; $x -le 520; $x+=8) { $c=$bmp.GetPixel($x,270); Write-Host ("x=$x : $($c.R),$($c.G),$($c.B)") }
$bmp.Dispose()
