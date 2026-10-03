param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$BuildDir = 'build/dev',
    [switch]$Phase
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
$initialState = if ($Phase) { [IO.File]::ReadAllBytes((Join-Path $data 'state.json')) }
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
if ($Phase) {
    # Unequal logical periods and asymmetric feet expose frame-index remapping and UV-only mirroring.
    $phaseColours = @('FF0000','00FF00','0000FF','FFFF00','FF00FF','00FFFF','FF8000','808080')
    $widths = @(48,40,32,56,32,40,56,48); $heights = @(48,40,48,32,40,48,32,48)
    $pivots = @(12,8,14,24,14,8,14,12)
    $atlas = [Drawing.Bitmap]::new(512,64)
    try {
        for ($i = 0; $i -lt 8; $i++) {
            $colour = [Drawing.ColorTranslator]::FromHtml('#' + $phaseColours[$i])
            for ($y = 0; $y -lt $heights[$i]; $y++) {
                for ($x = 0; $x -lt $widths[$i]; $x++) {
                    $foot = $y -ge $heights[$i]-2 -and $x -ge $pivots[$i]-2 -and $x -lt $pivots[$i]+2
                    $atlas.SetPixel($i*64+$x,$y,$(if ($foot) { [Drawing.Color]::White } else { $colour }))
                }
            }
        }
        $atlas.Save($texturePath,[Drawing.Imaging.ImageFormat]::Png)
    } finally { $atlas.Dispose() }
    $animation.width = 512; $animation.height = 64
    $animation.frames = @(0..7 | ForEach-Object { @{ x=$_*64; y=0; w=$widths[$_]; h=$heights[$_]; pivotX=$pivots[$_]; pivotY=$heights[$_] } })
    $animation.timeline = @(@{frame=0;seconds=.125},@{frame=1;seconds=.125},@{frame=2;seconds=.25},@{frame=3;seconds=.5})
    $animation.events = @(@{frame=0;name='phase-start';text='';value=0})
    $animation.directions = @{
        left = @{name='left';loop=$true;direction=0;timeline=@(@{frame=4;seconds=.125},@{frame=5;seconds=.375})
            events=@(@{frame=0;name='phase-left-entry';text='';value=0},@{frame=1;name='phase-left-step';text='';value=0})}
        up = @{name='reverse';loop=$true;direction=1;timeline=@(@{frame=5;seconds=.125},@{frame=6;seconds=.125},@{frame=7;seconds=.25});events=@()}
        up_right = @{name='bounce';loop=$true;direction=2;timeline=@(@{frame=2;seconds=.125},@{frame=4;seconds=.25},@{frame=6;seconds=.125});events=@()}
    }
    Write-Json $animationPath $animation
    $player.components | Add-Member -NotePropertyName ObjectBehavior -NotePropertyValue ([pscustomobject]@{
        connections=@();luaSource='return { on_event=function(self, name, payload) if name == "animation" then mye.log("phase-event:" .. payload.name) end end }'
    }) -Force
    Write-Json $scenePath $scene
    $down = @{ticks=9;x=0;y=-1}; $leftOne = @{ticks=1;x=-1;y=0}; $leftFour = @{ticks=4;x=-1;y=0}; $rightOne = @{ticks=1;x=1;y=0}
    $cases = @(
        @{name='phase-down';steps=@($down);colour=0x00FF00;events=0},
        @{name='phase-left-early';steps=@($down,$leftOne);colour=0xFF00FF;events=0},
        @{name='phase-left-later';steps=@($down,$leftFour);colour=0x00FFFF;events=1},
        @{name='phase-mirror';steps=@($down,$leftFour,$rightOne);colour=0x00FFFF;events=1},
        @{name='phase-reverse-early';steps=@($down,$leftFour,$rightOne,@{ticks=2;x=0;y=1});colour=0x808080;events=1},
        @{name='phase-reverse-later';steps=@($down,$leftFour,$rightOne,@{ticks=10;x=0;y=1});colour=0xFF8000;events=1},
        @{name='phase-bounce-early';steps=@($down,$leftFour,$rightOne,@{ticks=2;x=1;y=1});colour=0xFF00FF;events=1},
        @{name='phase-bounce-apex';steps=@($down,$leftFour,$rightOne,@{ticks=10;x=1;y=1});colour=0xFF8000;events=1},
        @{name='phase-bounce-back';steps=@($down,$leftFour,$rightOne,@{ticks=16;x=1;y=1});colour=0xFF00FF;events=1}
    )
    function Check-Phase([string]$Name,[int]$Colour) {
        Check-Pixel $Name 490 254 $Colour; Check-Pixel $Name 480 269 0xFFFFFF
        $bitmap = [Drawing.Bitmap]::new((Join-Path $run "$Name.bmp"))
        try { $background = $bitmap.GetPixel(0,0).ToArgb() -band 0xFFFFFF } finally { $bitmap.Dispose() }
        Check-Pixel $Name 480 272 $background
    }
    foreach ($case in $cases) {
        $inputFile = Replay $case.name $case.steps
        $log = Run-App $game $case.name @('--project',$project,'--headless','--input',$inputFile,'--dump',(Join-Path $run ($case.name+'.bmp')))
        Check-Phase $case.name $case.colour
        if ([regex]::Matches($log,'phase-event:phase-start').Count -ne 1 -or $log -match 'phase-event:phase-left-entry' -or
            [regex]::Matches($log,'phase-event:phase-left-step').Count -ne $case.events) { throw "Silent remap/actual crossing failed: $($case.name)" }
    }
    # Completed motions retain their terminal frame after turning to a different period.
    $animation.loop = $false
    foreach ($step in $animation.timeline) { $step.seconds = .001 }
    $animation.directions.left.timeline += @{frame=6;seconds=.001}
    foreach ($clip in $animation.directions.Values) { $clip.loop=$false; foreach ($step in $clip.timeline) { $step.seconds=.001 } }
    Write-Json $animationPath $animation
    $player.components.ObjectBehavior.luaSource = 'local n=0; return { on_update=function(self,dt) n=n+1; local e=mye.world.entity_from_packed(self.entity); if n==1 then e:face_move(mye.Vec2(0,-1)) elseif n==2 then e:face_move(mye.Vec2(-1,0)) elseif n==4 then e:face_move(mye.Vec2(1,0)); mye.log("phase-play-ready") end end }'
    Write-Json $scenePath $scene
    $log = Run-App $editor 'phase-play' @('--project',$project,'--headless','--play','--frames','12000','--dump',(Join-Path $run 'phase-play.bmp'))
    if ($log -notmatch 'phase-play-ready') { throw 'Completed Play did not reach its final turn' }
    Check-Phase 'phase-play' 0xFF8000
    # Endpoint sequences only: per-client phase is presentation, not a replicated animation clock.
    $player.components.ObjectBehavior.luaSource = 'return { on_init=function(self) mye.log("client-lua-must-not-run"); error("client authority violation") end }'
    Write-Json $scenePath $scene
    $down12=@{ticks=12;x=0;y=-1}; $right12=@{ticks=12;x=1;y=0}; $up12=@{ticks=12;x=0;y=1}; $stop30=@{ticks=30;x=0;y=0}
    foreach ($case in @(
        @{name='phase-online-mirror';steps=@($down12,$right12,$stop30);ticks=54;colour=0xFF8000;peerX=288;peerY=254},
        @{name='phase-online-reverse';steps=@($down12,$right12,$up12,$stop30);ticks=66;colour=0x00FFFF;peerX=288;peerY=312},
        @{name='phase-online-bounce';steps=@($down12,$right12,$up12,@{ticks=12;x=1;y=1},$stop30);ticks=78;colour=0xFF00FF;peerX=248;peerY=353}
    )) {
        $caseData = Join-Path $run ($case.name+'-data'); New-Item -ItemType Directory -Path $caseData | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $caseData 'state.json'),$initialState)
        $owned=@()
        try {
            $hostName=$case.name+'-server'
            $hostProcess=Start-App $server $hostName @('--data',$caseData,'--project',$project,'--port','0','--ticks','400'); $owned+=$hostProcess
            $timer=[Diagnostics.Stopwatch]::StartNew(); $port=0
            while ($timer.Elapsed.TotalSeconds -lt 5 -and -not $hostProcess.HasExited) {
                $log=Get-Content -LiteralPath (Join-Path $run ($hostName+'.log')) -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
                if ($log -match 'MyServer .*?port (\d+), tickrate 60Hz') { $port=[int]$Matches[1]; break }
                Start-Sleep -Milliseconds 25
            }
            if (-not $port) { throw "Server readiness failed: $hostName" }
            $bName=$case.name+'-b'; $aName=$case.name+'-a'
            $bInput=Replay $bName @($down12,@{ticks=30;x=-1;y=0},@{ticks=180;x=0;y=0}); $aInput=Replay $aName $case.steps
            foreach ($entry in @(@($bName,'directions-b',$bInput),@($aName,'directions-a',$aInput))) {
                $process=Start-App $game $entry[0] @('--project',$project,'--headless','--connect',"127.0.0.1:$port",'--credentials',(Join-Path $run ($entry[1]+'.json')),'--input',$entry[2],'--dump',(Join-Path $run ($entry[0]+'.bmp')))
                $owned+=$process; if ($entry[0] -eq $bName) { Start-Sleep -Milliseconds 300 }
            }
            $aLog=Wait-App $owned[2] $aName; $bLog=Wait-App $owned[1] $bName
            if ($aLog -notmatch "Input replay confirmed: steps=$($case.ticks), pending=0" -or $aLog -notmatch 'Network object added:' -or
                $bLog -notmatch 'Input replay confirmed: steps=222, pending=0') { throw "Authority replay failed: $($case.name)" }
            Check-Phase $aName $case.colour; Check-Phase $bName 0xFF8000
            Check-Pixel $aName $case.peerX $case.peerY 0xFF8000
            $null=Wait-App $hostProcess $hostName
        } finally {
            foreach ($process in $owned) { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }; $process.Dispose() }
        }
    }
    $player.components.PSObject.Properties.Remove('ObjectBehavior'); Write-Json $scenePath $scene
    Write-Output 'PASS: normalized unequal/reverse/ping-pong phase, silent remap, asymmetric feet, completed Play/online endpoints (continuous partial online timing remains pending)'
}
if ((Get-FileHash -LiteralPath $scenePath).Hash -ne $sceneHash -or
    (Get-FileHash -LiteralPath ($animationPath + '.meta')).Hash -ne $metaHash) { throw 'Authored scene/GUID changed' }
Write-Output "PASS: $Configuration saved 8-facing clips, direct/mirror/default pixels, editor preview/Play, strict rejection, two authoritative clients/Lua isolation ($run)"
