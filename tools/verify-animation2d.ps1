param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$BuildDir = 'build/dev'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repo $BuildDir }
$game = Join-Path $build "apps/game/$Configuration/MyGame.exe"
$editor = Join-Path $build "apps/editor/$Configuration/MyEditor.exe"
$server = Join-Path $build "apps/server/$Configuration/MyServer.exe"
foreach ($exe in @($game, $editor, $server)) {
    if (-not (Test-Path -LiteralPath $exe)) { throw "Build the apps first: $exe" }
}
$run = Join-Path $repo ('build/animation2d/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'game/starter/meadow_village') -Destination (Join-Path $run 'project') -Recurse
function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
}
function Start-App([string]$Exe, [string]$Name, [string[]]$Arguments) {
    $previousLocalData = $env:LOCALAPPDATA
    try {
        $env:LOCALAPPDATA = Join-Path $run 'localappdata'
        $process = Start-Process -FilePath $Exe -ArgumentList ($Arguments | ForEach-Object { '"' + $_ + '"' }) `
            -WorkingDirectory $repo -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $run "$Name.log") -RedirectStandardError (Join-Path $run "$Name.err")
    } finally { $env:LOCALAPPDATA = $previousLocalData }
    $null = $process.Handle
    return $process
}
function Wait-App($Process, [string]$Name, [int]$Expected = 0) {
    if (-not $Process.WaitForExit(15000)) { throw "$Name timed out: $run" }
    if ($Process.ExitCode -ne $Expected) { throw "$Name exit=$($Process.ExitCode), expected=$Expected : $run" }
    $log = Get-Content -LiteralPath (Join-Path $run "$Name.log") -Raw -Encoding UTF8
    if ($Expected -eq 0 -and $log -match '\[ERROR\]|client-lua-must-not-run') { throw "$Name error: $run" }
    return $log
}
function Run-App([string]$Exe, [string]$Name, [string[]]$Arguments, [int]$Expected = 0) {
    $process = Start-App $Exe $Name $Arguments
    try { return Wait-App $process $Name $Expected }
    finally { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }; $process.Dispose() }
}

$project = Join-Path $run 'project/project.myeproj'
$manifest = Get-Content -LiteralPath $project -Raw -Encoding UTF8 | ConvertFrom-Json
$scenePath = Join-Path (Split-Path -Parent $project) $manifest.mainScene
$scene = Get-Content -LiteralPath $scenePath -Raw -Encoding UTF8 | ConvertFrom-Json
$player = $scene.entities | Where-Object { $_.components.CharacterController2D.enabled }
if (@($player).Count -ne 1) { throw 'Expected one player prototype' }
$player.components.LocalTransform.px = 0; $player.components.LocalTransform.py = 0
$player.components.LocalTransform.sx = 1; $player.components.LocalTransform.sy = 1
$player.components.ObjectName.value = 'Player'
$pose = $player.components.LocalTransform | ConvertTo-Json | ConvertFrom-Json
$scene.entities = @($player, [pscustomobject]@{ id = 900; components = [pscustomobject]@{
    ObjectName = @{ value = 'Camera' }; LocalTransform = $pose
    Camera2D = @{ __version = 1; current = $true; followTarget = 'Player'; zoom = 2; deadzoneHalf = @{ x = 0; y = 0 } }
} })

# Diagnostic geometry: each facing owns one colour and a left-edge white marker.
# No user art or production assets are changed.
Add-Type -AssemblyName System.Drawing
$names = @('down', 'down_left', 'left', 'up_left', 'up', 'up_right', 'right', 'down_right')
$colours = @('FF0000', '00FF00', '0000FF', 'FFFF00', 'FF00FF', '00FFFF', '000000', 'FFFFFF', '808080')
$atlas = [Drawing.Bitmap]::new(432, 48)
try {
    for ($i = 0; $i -lt 9; $i++) {
        $colour = [Drawing.ColorTranslator]::FromHtml('#' + $colours[$i])
        for ($y = 0; $y -lt 48; $y++) {
            for ($x = 0; $x -lt 48; $x++) {
                $atlas.SetPixel($i * 48 + $x, $y, $(if ($x -lt 4) { [Drawing.Color]::White } else { $colour }))
            }
        }
    }
    $texturePath = Join-Path $run 'project/assets/characters/direction-check.png'
    $atlas.Save($texturePath, [Drawing.Imaging.ImageFormat]::Png)
} finally { $atlas.Dispose() }
$textureGuid = [Guid]::NewGuid().ToString(); $animationGuid = [Guid]::NewGuid().ToString()
Write-Json ($texturePath + '.meta') @{ guid = $textureGuid; importer = 'TextureImporter'; importerVersion = 1 }
$animationPath = Join-Path $run 'project/assets/animations/direction-check.anim'
Write-Json ($animationPath + '.meta') @{ guid = $animationGuid; importer = 'AnimationAsset'; importerVersion = 1 }
$directions = @{}
for ($i = 0; $i -lt 8; $i++) {
    $directions[$names[$i]] = @{ name = $names[$i]; loop = $true; direction = 0; timeline = @(@{ frame = $i; seconds = 1 }); events = @() }
}
$animation = @{ version = 2; texture = $textureGuid; width = 432; height = 48; name = 'fallback'; loop = $true; direction = 0
    frames = @(0..8 | ForEach-Object { @{ x = $_ * 48; y = 0; w = 48; h = 48; pivotX = 24; pivotY = 48 } })
    timeline = @(@{ frame = 8; seconds = 1 }); events = @(); directions = $directions; mirrorRight = $true }
Write-Json $animationPath $animation
foreach ($ref in @($player.components.SpriteAnimator.animation, $player.components.CharacterController2D.idleAnimation,
    $player.components.CharacterController2D.walkAnimation)) { $ref.guid = $animationGuid }
Write-Json $scenePath $scene
$sceneHash = (Get-FileHash -LiteralPath $scenePath).Hash
$metaHash = (Get-FileHash -LiteralPath ($animationPath + '.meta')).Hash
function Replay([string]$Name, [object[]]$Steps) {
    $path = Join-Path $run "$Name-input.json"
    Write-Json $path @{ version = 1; steps = $Steps }
    return $path
}
function Check-Pixel([string]$Name, [int]$X, [int]$Y, [int]$Colour) {
    $bitmap = [Drawing.Bitmap]::new((Join-Path $run "$Name.bmp"))
    try {
        if ($bitmap.Width -ne 960 -or $bitmap.Height -ne 540 -or
            ($bitmap.GetPixel($X, $Y).ToArgb() -band 0xFFFFFF) -ne $Colour) {
            throw "$Name pixel ($X,$Y) mismatch: $run"
        }
    } finally { $bitmap.Dispose() }
}
function Run-Facing([string]$Name, [double]$X, [double]$Y, [int]$Colour, [bool]$Flip = $false) {
    $inputFile = Replay $Name @(@{ ticks = 1; x = $X; y = $Y }, @{ ticks = 1; x = 0; y = 0 })
    $null = Run-App $game $Name @('--project', $project, '--headless', '--input', $inputFile, '--dump', (Join-Path $run "$Name.bmp"))
    Check-Pixel $Name 480 222 $Colour
    Check-Pixel $Name $(if ($Flip) { 524 } else { 435 }) 222 0xFFFFFF
    Check-Pixel $Name $(if ($Flip) { 435 } else { 524 }) 222 $Colour
}
$vectors = @(@(0,-1), @(-1,-1), @(-1,0), @(-1,1), @(0,1), @(1,1), @(1,0), @(1,-1))
for ($i = 0; $i -lt 8; $i++) { Run-Facing $names[$i] $vectors[$i][0] $vectors[$i][1] ([Convert]::ToInt32($colours[$i],16)) }
$right = $directions.right; $left = $directions.left
$directions.Remove('right'); Write-Json $animationPath $animation
Run-Facing 'mirrored-right' 1 0 0x0000FF $true
$directions.Remove('left'); Write-Json $animationPath $animation
Run-Facing 'fallback-right' 1 0 0x808080
$directions.right = $right; $directions.left = $left; Write-Json $animationPath $animation
$null = Run-App $editor 'editor' @('--project', $project, '--animation', 'assets/animations/direction-check.anim',
    '--frames', '12', '--dump', (Join-Path $run 'editor.bmp'))
# Play reads the same authored variants. This is a scripted facing check, not a physical input claim.
$player.components | Add-Member -NotePropertyName ObjectBehavior -NotePropertyValue ([pscustomobject]@{
    connections = @(); luaSource = "local marked = false; return { on_update = function(self, dt) mye.world.entity_from_packed(self.entity):face_move(mye.Vec2(1, 0)); if not marked then mye.log('direction-play-tick'); marked = true end end }"
}) -Force
Write-Json $scenePath $scene
$playLog = Run-App $editor 'play' @('--project', $project, '--headless', '--play', '--frames', '3000', '--dump', (Join-Path $run 'play.bmp'))
if ($playLog -notmatch 'direction-play-tick') { throw 'Play capture had no completed fixed tick' }
Check-Pixel 'play' 480 222 0x000000
$player.components.PSObject.Properties.Remove('ObjectBehavior'); Write-Json $scenePath $scene
$animation.directions.up.timeline[0].seconds = 0; Write-Json $animationPath $animation
$null = Run-App $game 'bad-game' @('--project', $project, '--headless', '--ticks', '1') 1
$null = Run-App $editor 'bad-editor' @('--project', $project, '--headless', '--frames', '1') 1
$animation.directions.up.timeline[0].seconds = 1; Write-Json $animationPath $animation
$animation.width = 433; Write-Json $animationPath $animation
$null = Run-App $game 'wrong-sheet-game' @('--project', $project, '--headless', '--ticks', '1') 1
$null = Run-App $editor 'wrong-sheet-editor' @('--project', $project, '--headless', '--frames', '1') 1
$animation.width = 432; Write-Json $animationPath $animation
$animation.nextAnimation = [Guid]::NewGuid().ToString(); $animation.loop = $false
foreach ($clip in $directions.Values) { $clip.loop = $false; $clip.timeline[0].seconds = .001 }
Write-Json $animationPath $animation
$null = Run-App $game 'missing-next-game' @('--project', $project, '--headless', '--ticks', '3') 1
$null = Run-App $editor 'missing-next-editor' @('--project', $project, '--headless', '--play', '--frames', '3000') 1
$animation.Remove('nextAnimation'); $animation.loop = $true
foreach ($clip in $directions.Values) { $clip.loop = $true; $clip.timeline[0].seconds = 1 }
Write-Json $animationPath $animation

# Authoritative snapshots must select the peer's facing, using the same saved v2 data.
$player.components | Add-Member -NotePropertyName ObjectBehavior -NotePropertyValue ([pscustomobject]@{
    connections = @(); luaSource = "return { on_init = function(self) mye.log('client-lua-must-not-run'); error('client authority violation') end }"
}) -Force
Write-Json $scenePath $scene
$data = Join-Path $run 'data'
foreach ($user in @('directions-a', 'directions-b')) {
    $null = Run-App $server "register-$user" @('--data', $data, '--register', $user, 'local-test-only')
    $null = Run-App $server "character-$user" @('--data', $data, '--make-char', $user, $user)
    Write-Json (Join-Path $run "$user.json") @{ username = $user; password = 'local-test-only' }
}
$owned = @()
try {
    $hostProcess = Start-App $server 'server' @('--data', $data, '--project', $project, '--port', '0', '--ticks', '300')
    $owned += $hostProcess
    $timer = [Diagnostics.Stopwatch]::StartNew(); $port = 0
    while ($timer.Elapsed.TotalSeconds -lt 5 -and -not $hostProcess.HasExited) {
        $log = Get-Content -LiteralPath (Join-Path $run 'server.log') -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
        if ($log -match 'MyServer .*?port (\d+), tickrate 60Hz') { $port = [int]$Matches[1]; break }
        Start-Sleep -Milliseconds 25
    }
    if (-not $port) { throw "Server readiness failed: $run" }
    $bInput = Replay 'online-b' @(@{ ticks = 40; x = -1; y = 0 }, @{ ticks = 180; x = 0; y = 0 })
    $aInput = Replay 'online-a' @(@{ ticks = 40; x = 1; y = 0 }, @{ ticks = 30; x = 0; y = 0 })
    foreach ($entry in @(@('b','directions-b',$bInput), @('a','directions-a',$aInput))) {
        $process = Start-App $game "online-$($entry[0])" @('--project', $project, '--headless', '--connect', "127.0.0.1:$port",
            '--credentials', (Join-Path $run "$($entry[1]).json"), '--input', $entry[2], '--dump', (Join-Path $run "online-$($entry[0]).bmp"))
        $owned += $process
        if ($entry[0] -eq 'b') { Start-Sleep -Milliseconds 300 }
    }
    $aLog = Wait-App $owned[2] 'online-a'; $bLog = Wait-App $owned[1] 'online-b'
    if ($aLog -notmatch 'Input replay confirmed: steps=70, pending=0' -or $aLog -notmatch 'Network object added:' -or
        $bLog -notmatch 'Input replay confirmed: steps=220, pending=0') { throw 'Authority/facing replay not confirmed' }
    Check-Pixel 'online-a' 480 222 0x000000
    Check-Pixel 'online-a' 96 222 0x0000FF
    Check-Pixel 'online-b' 480 222 0x0000FF
    $null = Wait-App $hostProcess 'server'
} finally {
    foreach ($process in $owned) {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}
$player.components.PSObject.Properties.Remove('ObjectBehavior'); Write-Json $scenePath $scene
if ((Get-FileHash -LiteralPath $scenePath).Hash -ne $sceneHash -or
    (Get-FileHash -LiteralPath ($animationPath + '.meta')).Hash -ne $metaHash) { throw 'Authored scene/GUID changed' }
Write-Output "PASS: $Configuration saved 8-facing clips, direct/mirror/default pixels, editor preview/Play, strict rejection, two authoritative clients/Lua isolation ($run)"
