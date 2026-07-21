# verify.ps1 — fast pixel verification for bridge_demo dumps using LockBits.
#
# Single-pass classification of all role colors + per-scenario depth-ordering assertions.
# Roles classified from sRGB backbuffer (colors brightened vs source linear values):
#   A(cyan) B(magenta) P(yellow) STATUE(white) GROUND(green) BRIDGE(tan) BG(clear)
#
# Usage: powershell -File verify.ps1 -Path dump.bmp [-Scenario over_bridge|under_bridge|behind_statue|front_statue|slope]
param(
    [Parameter(Mandatory=$true)][string]$Path,
    [string]$Scenario = ''
)
Add-Type -AssemblyName System.Drawing
$full = (Resolve-Path $Path).Path
$bmp = [System.Drawing.Bitmap]::FromFile($full)
$W = $bmp.Width; $H = $bmp.Height
$rect = New-Object System.Drawing.Rectangle 0,0,$W,$H
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$bytes = New-Object byte[] ($stride * $H)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
$bmp.UnlockBits($data)
$bmp.Dispose()

# accumulate counts + bbox + centroid per role
$roles = 'A','B','P','STATUE','GROUND','BRIDGE','BG'
$cnt=@{}; $minX=@{}; $minY=@{}; $maxX=@{}; $maxY=@{}; $sumX=@{}; $sumY=@{}
foreach($r in $roles){$cnt[$r]=0;$minX[$r]=99999;$minY[$r]=99999;$maxX[$r]=-1;$maxY[$r]=-1;$sumX[$r]=0.0;$sumY[$r]=0.0}

for ($y=0; $y -lt $H; $y++) {
  $row = $y * $stride
  for ($x=0; $x -lt $W; $x++) {
    $i = $row + $x*4
    $b = $bytes[$i]; $g = $bytes[$i+1]; $r = $bytes[$i+2]
    $role = 'BG'
    if ($r -lt 140 -and $g -gt 190 -and $b -gt 190) { $role='A' }
    elseif ($r -gt 190 -and $g -lt 150 -and $b -gt 190) { $role='B' }
    elseif ($r -gt 200 -and $g -gt 190 -and $b -lt 150) { $role='P' }
    elseif ($r -gt 210 -and $g -gt 210 -and $b -gt 210) { $role='STATUE' }
    elseif ($r -ge 180 -and $r -le 245 -and $g -ge 165 -and $g -le 225 -and $b -ge 115 -and $b -le 205 -and $g -ge $b) { $role='BRIDGE' }
    elseif ($g -gt $r -and $g -gt $b -and $g -gt 90) { $role='GROUND' }
    $cnt[$role]++
    if ($x -lt $minX[$role]){$minX[$role]=$x}
    if ($y -lt $minY[$role]){$minY[$role]=$y}
    if ($x -gt $maxX[$role]){$maxX[$role]=$x}
    if ($y -gt $maxY[$role]){$maxY[$role]=$y}
    $sumX[$role]+=$x; $sumY[$role]+=$y
  }
}

Write-Host "=== $Path ($W x $H) scenario=$Scenario ==="
foreach($role in $roles){
  if($cnt[$role] -gt 0){
    $cx=[int]($sumX[$role]/$cnt[$role]); $cy=[int]($sumY[$role]/$cnt[$role])
    Write-Host ("{0,-8} count={1,-7} box=[{2},{3}]-[{4},{5}] centroid=({6},{7})" -f $role,$cnt[$role],$minX[$role],$minY[$role],$maxX[$role],$maxY[$role],$cx,$cy)
  } else {
    Write-Host ("{0,-8} count=0" -f $role)
  }
}

function Box($role){ if($cnt[$role]-gt 0){@{minX=$minX[$role];minY=$minY[$role];maxX=$maxX[$role];maxY=$maxY[$role];cx=[int]($sumX[$role]/$cnt[$role]);cy=[int]($sumY[$role]/$cnt[$role]);n=$cnt[$role]}}else{$null} }

$pass = $true
$notes = @()
switch ($Scenario) {
  'over_bridge' {
    # A visible on bridge; B fully occluded by bridge deck.
    if ($cnt['A'] -lt 30) { $pass=$false; $notes+="FAIL: A not visible (count=$($cnt['A']))" } else { $notes+="A visible count=$($cnt['A'])" }
    if ($cnt['B'] -gt 15) { $pass=$false; $notes+="FAIL: B not occluded (count=$($cnt['B']) should be ~0)" } else { $notes+="B occluded by bridge deck count=$($cnt['B'])" }
    if ($cnt['BRIDGE'] -lt 200) { $pass=$false; $notes+="FAIL: bridge deck not rendered (count=$($cnt['BRIDGE']))" } else { $notes+="bridge deck rendered count=$($cnt['BRIDGE'])" }
  }
  'under_bridge' {
    # B partly visible: lower body pokes out BELOW the deck; upper body occluded by deck.
    $bb=Box 'B'; $br=Box 'BRIDGE'
    if ($cnt['B'] -lt 15) { $pass=$false; $notes+="FAIL: B not visible at all (count=$($cnt['B']))" } else { $notes+="B partially visible count=$($cnt['B'])" }
    if ($cnt['BRIDGE'] -lt 200) { $pass=$false; $notes+="FAIL: bridge deck missing" } else { $notes+="bridge deck rendered count=$($cnt['BRIDGE'])" }
    if($bb -and $br){
      $notes+="B visible box=[$($bb.minX),$($bb.minY)]-[$($bb.maxX),$($bb.maxY)] ; bridge deck bottom=$($br.maxY)"
      # Occlusion proof: B's visible pixels start BELOW the deck's bottom edge (deck hides upper body).
      if ($bb.minY -ge ($br.maxY - 4)) { $notes+="B's visible pixels are below deck bottom -> upper body occluded by deck" }
      else { $pass=$false; $notes+="FAIL: B visible above deck bottom (not occluded) B.minY=$($bb.minY) deck.maxY=$($br.maxY)" }
    }
  }
  'behind_statue' {
    # Statue occludes player: player pixels heavily reduced / statue overlaps P centroid.
    $notes+="P count=$($cnt['P']) STATUE count=$($cnt['STATUE'])"
    if ($cnt['STATUE'] -lt 500) { $pass=$false; $notes+="FAIL: statue not rendered" }
    if ($cnt['P'] -gt 120) { $pass=$false; $notes+="FAIL: player not occluded by statue (count=$($cnt['P']))" } else { $notes+="player occluded behind statue count=$($cnt['P'])" }
  }
  'front_statue' {
    $notes+="P count=$($cnt['P']) STATUE count=$($cnt['STATUE'])"
    if ($cnt['P'] -lt 120) { $pass=$false; $notes+="FAIL: player should be fully visible in front (count=$($cnt['P']))" } else { $notes+="player visible in front of statue count=$($cnt['P'])" }
  }
  'slope' {
    $pp=Box 'P'; $bb=Box 'B'
    if(-not $pp){ $pass=$false; $notes+="FAIL: player(P) not visible on slope" }
    elseif(-not $bb){ $pass=$false; $notes+="FAIL: flat reference(B) not visible" }
    else {
      $notes+="P(on slope) feet=$($pp.maxY) centroid=($($pp.cx),$($pp.cy))"
      $notes+="B(flat ref) feet=$($bb.maxY) centroid=($($bb.cx),$($bb.cy))"
      # P should be rendered higher on slope: feet Y (screen) smaller than flat B by ~ rise px.
      if ($pp.maxY -lt ($bb.maxY - 20)) { $notes+="P raised by $([int]($bb.maxY-$pp.maxY))px above flat ref (slope height applied)" }
      else { $pass=$false; $notes+="FAIL: P not raised vs flat ref (P feet=$($pp.maxY) B feet=$($bb.maxY))" }
    }
  }
}
foreach($n in $notes){ Write-Host "  $n" }
if($Scenario){ $res='FAIL'; if($pass){$res='PASS'}; Write-Host "RESULT: $res" }
