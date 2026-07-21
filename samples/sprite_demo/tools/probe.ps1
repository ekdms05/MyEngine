param([Parameter(Mandatory=$true)][string]$Path)
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap($Path)
$W=$bmp.Width;$H=$bmp.Height
$rect = New-Object System.Drawing.Rectangle(0,0,$W,$H)
$data = $bmp.LockBits($rect,[System.Drawing.Imaging.ImageLockMode]::ReadOnly,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride=$data.Stride
$bytes = New-Object byte[] ($stride*$H)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0,$bytes,0,$bytes.Length)
$bmp.UnlockBits($data)
function Px([int]$x,[int]$y){$o=$y*$stride+$x*4; return @($bytes[$o+2],$bytes[$o+1],$bytes[$o],$bytes[$o+3])}

Write-Host "== $Path ($W x $H) =="
# top-left 20 pixels sample (imgui region)
Write-Host "top-left samples:"
foreach($pt in @(@(15,15),@(30,30),@(50,20),@(100,40),@(20,60),@(15,100))){
    $p=Px $pt[0] $pt[1]
    Write-Host ("  ({0},{1}) = R{2} G{3} B{4} A{5}" -f $pt[0],$pt[1],$p[0],$p[1],$p[2],$p[3])
}
# hero head region around center-top. center ~ (480, 270 bottom of hero). Head is above.
Write-Host "hero column x=480 y=200..300:"
for($y=200;$y -le 300;$y+=8){
    $p=Px 480 $y
    Write-Host ("  y={0} = R{1} G{2} B{3}" -f $y,$p[0],$p[1],$p[2])
}
# horizontal scanline near vertical center to see background tiles
Write-Host "scanline y=100 tile colors (x every 24):"
$line=""
for($x=0;$x -lt [math]::Min($W,960);$x+=48){
    $p=Px $x 100
    $line += ("[{0}:{1},{2},{3}]" -f $x,$p[0],$p[1],$p[2])
}
Write-Host $line
$bmp.Dispose()
