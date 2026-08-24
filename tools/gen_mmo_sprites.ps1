# gen_mmo_sprites.ps1 — 아기자기한 3직업 + 몹 도트 스프라이트 생성(48x48 PNG, 투명 배경)
# 검사(빨강+검)·마법사(파랑+모자+지팡이)·버퍼(초록+후광)·슬라임(보라 몹).
Add-Type -AssemblyName System.Drawing

$dir = "E:\MyEngine\samples\mmo_demo\assets\sprites"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$S = 48

function New-Canvas {
    $bmp = New-Object System.Drawing.Bitmap($S, $S, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $g.Clear([System.Drawing.Color]::FromArgb(0,0,0,0))
    return @($bmp, $g)
}
function C([int]$r,[int]$g,[int]$b) { return [System.Drawing.Color]::FromArgb(255,$r,$g,$b) }
function Brush($col) { return New-Object System.Drawing.SolidBrush($col) }
function Save($bmp,$name) { $bmp.Save((Join-Path $dir $name), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose() }

$skin = C 245 205 165
$dark = C 40 30 40

# ---- 검사(Swordsman): 빨강 튜닉 + 은빛 검 ----
$c = New-Canvas; $bmp=$c[0]; $g=$c[1]
$g.FillEllipse((Brush (C 190 55 55)), 12, 22, 26, 22)          # 몸(빨강)
$g.FillEllipse((Brush $skin), 15, 8, 20, 20)                   # 머리
$g.FillRectangle((Brush (C 150 150 160)), 15, 8, 20, 5)        # 투구 밴드
$g.FillRectangle((Brush $dark), 20, 18, 3, 3)                  # 눈
$g.FillRectangle((Brush $dark), 28, 18, 3, 3)
$g.FillRectangle((Brush (C 215 215 230)), 39, 6, 4, 34)        # 검날
$g.FillRectangle((Brush (C 120 80 40)), 36, 38, 10, 4)         # 검 손잡이
Save $bmp "swordsman.png"

# ---- 마법사(Mage): 파랑 로브 + 뾰족 모자 + 지팡이 ----
$c = New-Canvas; $bmp=$c[0]; $g=$c[1]
$g.FillEllipse((Brush (C 70 95 205)), 12, 22, 26, 24)          # 로브(파랑)
$g.FillEllipse((Brush $skin), 16, 12, 18, 18)                  # 머리
$hat = New-Object System.Drawing.Drawing2D.GraphicsPath
$pts = @([System.Drawing.Point]::new(12,16),[System.Drawing.Point]::new(38,16),[System.Drawing.Point]::new(25,-2))
$hat.AddPolygon($pts)
$g.FillPath((Brush (C 45 55 130)), $hat)                       # 뾰족 모자
$g.FillRectangle((Brush $dark), 20, 20, 3, 3)                  # 눈
$g.FillRectangle((Brush $dark), 28, 20, 3, 3)
$g.FillRectangle((Brush (C 130 90 50)), 6, 14, 3, 30)          # 지팡이
$g.FillEllipse((Brush (C 255 235 120)), 2, 8, 11, 11)          # 지팡이 보석(노란 빛)
Save $bmp "mage.png"

# ---- 버퍼(Buffer): 초록/흰 로브 + 노란 후광 + 반짝 ----
$c = New-Canvas; $bmp=$c[0]; $g=$c[1]
$g.FillEllipse((Brush (C 245 245 245)), 12, 22, 26, 24)        # 흰 로브
$g.FillEllipse((Brush (C 110 200 140)), 14, 34, 22, 12)       # 초록 밑단
$g.FillEllipse((Brush $skin), 16, 12, 18, 18)                  # 머리
$g.DrawEllipse((New-Object System.Drawing.Pen((C 255 225 90),3)), 15, 3, 20, 8)  # 후광(노란 링)
$g.FillRectangle((Brush $dark), 20, 20, 3, 3)                  # 눈
$g.FillRectangle((Brush $dark), 28, 20, 3, 3)
$g.FillRectangle((Brush (C 255 235 120)), 40, 12, 3, 3)       # 반짝
$g.FillRectangle((Brush (C 255 235 120)), 6, 30, 3, 3)
Save $bmp "buffer.png"

# ---- 슬라임(Monster): 보라 몹, 귀여운 얼굴 ----
$c = New-Canvas; $bmp=$c[0]; $g=$c[1]
$g.FillEllipse((Brush (C 155 85 205)), 6, 16, 36, 30)          # 보라 몸
$g.FillEllipse((Brush (C 180 120 225)), 12, 20, 12, 8)        # 하이라이트
$g.FillRectangle((Brush (C 255 255 255)), 16, 28, 6, 6)  # 눈 흰자
$g.FillRectangle((Brush (C 255 255 255)), 28, 28, 6, 6)
$g.FillRectangle((Brush $dark), 18, 30, 3, 3)                 # 눈동자
$g.FillRectangle((Brush $dark), 30, 30, 3, 3)
Save $bmp "slime.png"

Write-Host "생성 완료: swordsman/mage/buffer/slime -> $dir"
Get-ChildItem $dir -Filter *.png | ForEach-Object { Write-Host ("  " + $_.Name + " (" + $_.Length + " bytes)") }
