# package.ps1 — MyEngine 배포 번들 생성 (docs/mmorpg/09, M11)
#
# 정적 링크(별도 DLL 불필요)라서 배포는 exe + 에셋 .pak + 기본 서버 설정을 한 폴더로 묶고
# zip 으로 압축하면 된다. 클라(MyGame)·서버(MyServer)·에디터(MyEditor)를 함께 담는다.
#
# 사용:
#   pwsh tools/package/package.ps1                       # Release, build/dev 기준, dist/ 로 출력
#   pwsh tools/package/package.ps1 -Config Debug         # 이미 빌드된 Debug 바이너리로 번들
#   pwsh tools/package/package.ps1 -Version 0.2.0 -OutDir C:\out
param(
    [string]$Config  = "Release",
    [string]$BuildDir = "build/dev",
    [string]$OutDir  = "dist",
    [string]$Version = "0.1.0",
    [switch]$NoZip
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # tools/package → repo 루트
Set-Location $repo
Write-Host "[package] repo=$repo config=$Config version=$Version"

# apps 폴더명(game/server/editor/paktool)과 타깃명(MyGame 등)이 다르므로 경로를 직접 해석한다:
#   $BuildDir/apps/<folder>/<Config>/<Target>.exe
function Resolve-Bin([string]$folder, [string]$target) {
    $p = Join-Path $repo "$BuildDir/apps/$folder/$Config/$target.exe"
    if (-not (Test-Path $p)) { throw "실행파일 없음: $p" }
    return $p
}

$gameExe    = Resolve-Bin "game"    "MyGame"
$serverExe  = Resolve-Bin "server"  "MyServer"
$editorExe  = Resolve-Bin "editor"  "MyEditor"
$paktoolExe = Resolve-Bin "paktool" "paktool"

# 출력 스테이징 폴더(OutDir 이 절대경로면 그대로, 상대면 repo 기준).
$stageName = "MyEngine-$Version-$Config"
if ([System.IO.Path]::IsPathRooted($OutDir)) { $outRoot = $OutDir }
else { $outRoot = Join-Path $repo $OutDir }
$stage = Join-Path $outRoot $stageName
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# 실행파일 복사.
Copy-Item $gameExe   (Join-Path $stage "MyGame.exe")
Copy-Item $serverExe (Join-Path $stage "MyServer.exe")
Copy-Item $editorExe (Join-Path $stage "MyEditor.exe")
Write-Host "[package] 실행파일 3종 복사 완료"

# 에셋을 .pak 으로 쿡(있으면).
$assets = Join-Path $repo "assets"
if (Test-Path $assets) {
    $pak = Join-Path $stage "game.pak"
    & $paktoolExe pack $assets $pak
    if ($LASTEXITCODE -ne 0) { throw "paktool pack 실패 (exit $LASTEXITCODE)" }
    Write-Host "[package] 에셋 → game.pak 쿡 완료"
} else {
    Write-Host "[package] assets/ 없음 — .pak 건너뜀"
}

# 기본 서버 설정(운영자가 조정할 시작점).
$cfgDir = Join-Path $stage "server_data"
New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null
$defaultConfig = @'
{
  "cvars": { "tickrate": 20, "move_speed": 6.0, "max_violations": 10, "max_backups": 10 },
  "flags": { "maintenance": false }
}
'@
Set-Content -Path (Join-Path $cfgDir "config.json") -Value $defaultConfig -Encoding utf8

# 버전/실행 안내.
$readme = @"
MyEngine $Version ($Config)

포함:
  MyGame.exe    — 게임 클라이언트/런타임 (사용법: MyGame.exe --project <dir> --scene <name>)
  MyServer.exe  — 헤드리스 게임 서버   (사용법: MyServer.exe --data server_data --port 27015)
  MyEditor.exe  — 에디터
  game.pak      — 쿡된 에셋 아카이브
  server_data/config.json — 서버 CVar/피처플래그/점검모드(핫리로드)

서버 계정 등록:  MyServer.exe --data server_data --register <user> <pass>
서버 실행:       MyServer.exe --data server_data
빌드: $(Get-Date -Format 'yyyy-MM-dd')
"@
Set-Content -Path (Join-Path $stage "README.txt") -Value $readme -Encoding utf8
Set-Content -Path (Join-Path $stage "version.txt") -Value "$Version $Config" -Encoding utf8

Write-Host "[package] 스테이징 완료 → $stage"

# zip 압축.
if (-not $NoZip) {
    $zip = "$stage.zip"
    if (Test-Path $zip) { Remove-Item -Force $zip }
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
    Write-Host "[package] 압축 완료 → $zip"
}

Write-Host "[package] 완료."
