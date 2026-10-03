param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$BuildDir = 'build/dev',
    [switch]$Phase,
    [switch]$Temporal,
    [switch]$States,
    [switch]$StateMaps,
    [switch]$Actions
)
$ErrorActionPreference = 'Stop'
if ($Temporal) { $Phase = $true }
if ($StateMaps -or $Actions) { $States = $true }
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
function Wait-App($Process, [string]$Name, [int]$Expected = 0, [int]$Timeout = 15000) {
    if (-not $Process.WaitForExit($Timeout)) { throw "$Name timed out after $Timeout ms: $run" }
    if ($Process.ExitCode -ne $Expected) { throw "$Name exit=$($Process.ExitCode), expected=$Expected : $run" }
    $log = Get-Content -LiteralPath (Join-Path $run "$Name.log") -Raw -Encoding UTF8
    if ($Expected -eq 0 -and $log -match '\[ERROR\]|client-lua-must-not-run') { throw "$Name error: $run" }
    return $log
}
function Run-App([string]$Exe, [string]$Name, [string[]]$Arguments, [int]$Expected = 0, [int]$Timeout = 15000) {
    $process = Start-App $Exe $Name $Arguments
    try { return Wait-App $process $Name $Expected $Timeout }
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
$initialState = if ($Phase -or $States) { [IO.File]::ReadAllBytes((Join-Path $data 'state.json')) }
$owned = @()
try {
    # Admission/RHI startup also consumes server time; 600 ticks covers both bounded client replays.
    $hostProcess = Start-App $server 'server' @('--data', $data, '--project', $project, '--port', '0', '--ticks', '600')
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
if ($States) {
    # Existing GUID/input/capture helpers; all diagnostic data stays in this new build fixture.
    $stateGuid=[Guid]::NewGuid().ToString();$idleGuid=[Guid]::NewGuid().ToString();$walkGuid=[Guid]::NewGuid().ToString()
    $attackGuid=[Guid]::NewGuid().ToString();$attackTextureGuid=[Guid]::NewGuid().ToString()
    $idlePath=Join-Path $run 'project/assets/animations/state-idle.anim';$walkPath=Join-Path $run 'project/assets/animations/state-walk.anim'
    $attackPath=Join-Path $run 'project/assets/animations/state-attack.anim';$statePath=Join-Path $run 'project/assets/animations/actor.animstate'
    $attackTexturePath=Join-Path $run 'project/assets/characters/state-attack.png'
    $image=[Drawing.Bitmap]::new(48,48)
    try { for($y=0;$y -lt 48;$y++){for($x=0;$x -lt 48;$x++){$image.SetPixel($x,$y,[Drawing.Color]::FromArgb(255,255,136,0))}};$image.Save($attackTexturePath,[Drawing.Imaging.ImageFormat]::Png) }
    finally {$image.Dispose()}
    Write-Json ($attackTexturePath+'.meta') @{guid=$attackTextureGuid;importer='TextureImporter';importerVersion=1}
    $idle=$animation | ConvertTo-Json -Depth 32 | ConvertFrom-Json
    $idle.PSObject.Properties.Remove('directions');$idle.PSObject.Properties.Remove('mirrorRight');$idle.version=1;$idle.name='idle'
    $idle.timeline[0].seconds=60;$idle.events=@(@{frame=0;name='state-idle';text='';value=0})
    $walk=$animation | ConvertTo-Json -Depth 32 | ConvertFrom-Json
    $walk.name='walk';$walk.timeline[0].seconds=60
    foreach($clip in $walk.directions.PSObject.Properties.Value){$clip.timeline[0].seconds=60;$clip.events=@(@{frame=0;name='state-walk';text='';value=0})}
    # Graph completion owns transitions; the missing single-clip successor must be ignored.
    $attack=@{version=1;texture=$attackTextureGuid;width=48;height=48;name='attack';loop=$false;direction=0;nextAnimation=[Guid]::NewGuid().ToString()
        frames=@(@{x=0;y=0;w=48;h=48;pivotX=24;pivotY=48});timeline=@(@{frame=0;seconds=1.0/60},@{frame=0;seconds=1.0/60});events=@(@{frame=0;name='state-attack';text='';value=0})}
    foreach($entry in @(@($idlePath,$idleGuid,$idle),@($walkPath,$walkGuid,$walk),@($attackPath,$attackGuid,$attack))){Write-Json $entry[0] $entry[2];Write-Json ($entry[0]+'.meta') @{guid=$entry[1];importer='AnimationAsset';importerVersion=1}}
    $graph=@{version=1;name='actor';initialState='idle'
        parameters=@(@{name='moving';type='bool';default=$false},@{name='ready';type='bool';default=$false},@{name='seed';type='float';default=.375},@{name='attack';type='trigger';default=$false})
        states=@(@{name='idle';animation=$idleGuid},@{name='walk';animation=$walkGuid},@{name='attack';animation=$attackGuid})
        transitions=@(
            @{from='*';to='attack';onClipFinished=$false;keepPhase=$false;conditions=@(@{param='attack';op='is_true'});consumeTriggers=@()},
            @{from='attack';to='idle';onClipFinished=$true;keepPhase=$false;conditions=@();consumeTriggers=@()},
            @{from='idle';to='walk';onClipFinished=$false;keepPhase=$false;conditions=@(@{param='moving';op='is_true'});consumeTriggers=@()},
            @{from='walk';to='idle';onClipFinished=$false;keepPhase=$false;conditions=@(@{param='moving';op='is_false'});consumeTriggers=@()})}
    Write-Json $statePath $graph;Write-Json ($statePath+'.meta') @{guid=$stateGuid;importer='AnimationStateAsset';importerVersion=1}
    $player.components.SpriteAnimator | Add-Member -NotePropertyName stateMachine -NotePropertyValue @{guid=$stateGuid;type='0'} -Force
    $player.components.SpriteAnimator.__version=2
    $player.components | Add-Member -NotePropertyName ObjectBehavior -NotePropertyValue ([pscustomobject]@{connections=@();luaSource=@'
local n=0
return {
    on_init=function(self)
        local e=mye.world.entity_from_packed(self.entity)
        assert(e:get_float('seed')==0.375 and not e:get_bool('ready'))
        e:set_bool('ready',true); mye.log('state-init-defaults')
    end,
    on_update=function(self,dt)
        n=n+1;local e=mye.world.entity_from_packed(self.entity)
        assert(e:get_bool('ready'));if n==3 then e:set_trigger('attack') end
        if n==6 then mye.log('state-six-ticks') end
    end,
    on_event=function(self,name,payload)
        if name=='animation' then mye.log('state-marker:'..payload.name) end
    end
}
'@}) -Force
    Write-Json $scenePath $scene
    $stateBytes=[IO.File]::ReadAllBytes($statePath);$attackBytes=[IO.File]::ReadAllBytes($attackPath)
    $savedGraphHash=(Get-FileHash -LiteralPath $statePath).Hash;$savedStateMetaHash=(Get-FileHash -LiteralPath ($statePath+'.meta')).Hash
    $stateInput=Replay 'state-local' @(@{ticks=1;x=0;y=0},@{ticks=1;x=1;y=0},@{ticks=3;x=0;y=0},@{ticks=1;x=1;y=0},@{ticks=1;x=0;y=0})
    $log=Run-App $game 'state-local' @('--project',$project,'--headless','--input',$stateInput,'--capture-at','1,2,3,4,5,6,7','--dump',(Join-Path $run 'state-local.bmp'))
    if($log -notmatch 'state-init-defaults' -or $log -notmatch 'state-six-ticks' -or ([regex]::Matches($log,'state-marker:state-attack')).Count -ne 1 -or ([regex]::Matches($log,'state-marker:state-idle')).Count -ne 3){throw 'Local state defaults/entry/completion failed'}
    $expected=@('idle','walk','attack','attack','idle','walk','idle');$stateColours=@(0x808080,0x000000,0xFF8800,0xFF8800,0x808080,0x000000,0x808080)
    for($i=1;$i -le 7;$i++){
        $capture=Get-Content -LiteralPath (Join-Path $run "state-local.step-$i.json") -Raw -Encoding UTF8 | ConvertFrom-Json
        if($capture.replayStep -ne $i -or $capture.actors.Count -ne 1 -or $capture.actors[0].animationState -ne $expected[$i-1]){throw "Local state step $i failed"}
        Check-Pixel "state-local.step-$i" 480 222 $stateColours[$i-1]
    }
    $playLog=Run-App $editor 'state-play' @('--project',$project,'--animation','assets/animations/state-idle.anim','--animation-state','assets/animations/actor.animstate','--headless','--play','--frames','12000','--dump',(Join-Path $run 'state-play.bmp'))
    if($playLog -notmatch 'state-init-defaults' -or $playLog -notmatch 'state-six-ticks' -or ([regex]::Matches($playLog,'state-marker:state-attack')).Count -ne 1 -or ([regex]::Matches($playLog,'state-marker:state-idle')).Count -ne 2){throw 'Play state binding/entry failed'}
    Check-Pixel 'state-play' 480 222 0x808080
    # All states, including inactive ones, are validated before Lua initialization.
    foreach($case in @('wrong-graph-guid','wrong-clip-guid','bad-clip','wrong-image-size','missing-texture')){
        switch($case){
            'wrong-graph-guid' {$player.components.SpriteAnimator.stateMachine.guid=$textureGuid}
            'wrong-clip-guid' {$graph.states[2].animation=$textureGuid;Write-Json $statePath $graph}
            'bad-clip' {$attack.timeline[0].seconds=0;Write-Json $attackPath $attack}
            'wrong-image-size' {$attack.width=49;Write-Json $attackPath $attack}
            'missing-texture' {$attack.texture=[Guid]::NewGuid().ToString();Write-Json $attackPath $attack}
        }
        Write-Json $scenePath $scene
        foreach($entry in @(@($game,'game'),@($editor,'editor'))){
            $arguments=@('--project',$project,'--headless')+$(if($entry[1] -eq 'game'){@('--ticks','1')}else{@('--play','--frames','3000')})
            $badLog=Run-App $entry[0] "state-$case-$($entry[1])" $arguments 1
            if($badLog -match 'state-init-defaults'){throw "Invalid $case executed Lua init"}
        }
        $player.components.SpriteAnimator.stateMachine.guid=$stateGuid;$graph.states[2].animation=$attackGuid
        $attack.timeline[0].seconds=1.0/60;$attack.width=48;$attack.texture=$attackTextureGuid
        [IO.File]::WriteAllBytes($statePath,$stateBytes);[IO.File]::WriteAllBytes($attackPath,$attackBytes);Write-Json $scenePath $scene
    }
    if ($Actions) {
        $savedActionScene = [IO.File]::ReadAllBytes($scenePath)
        $hurtGuid = [Guid]::NewGuid().ToString(); $deadGuid = [Guid]::NewGuid().ToString()
        $hurtPath = Join-Path $run 'project/assets/animations/state-hurt.anim'
        $deadPath = Join-Path $run 'project/assets/animations/state-dead.anim'
        $actionClip = $attack | ConvertTo-Json -Depth 32 | ConvertFrom-Json
        $actionClip.PSObject.Properties.Remove('nextAnimation')
        $actionClip.frames = @(0..2 | ForEach-Object { @{x=0;y=0;w=48;h=48;pivotX=24;pivotY=48} })
        $actionClip.timeline = @(0..2 | ForEach-Object { @{frame=$_;seconds=1.0/60} })
        $actionClip.events = @(@{frame=0;name='action-attack';text='';value=0}, @{frame=2;name='action-hit';text='';value=0})
        Write-Json $attackPath $actionClip
        foreach ($entry in @(@($hurtPath,$hurtGuid,'hurt'), @($deadPath,$deadGuid,'dead'))) {
            $clip = $actionClip | ConvertTo-Json -Depth 32 | ConvertFrom-Json
            $clip.name = $entry[2]; $clip.events = @(@{frame=0;name=('action-'+$entry[2]);text='';value=0})
            Write-Json $entry[0] $clip
            Write-Json ($entry[0]+'.meta') @{guid=$entry[1];importer='AnimationAsset';importerVersion=1}
        }
        $actionGraph = $graph | ConvertTo-Json -Depth 32 | ConvertFrom-Json
        $actionGraph.parameters += @(@{name='hurt';type='trigger';default=$false}, @{name='dead';type='bool';default=$false})
        $actionGraph.states += @(@{name='hurt';animation=$hurtGuid}, @{name='dead';animation=$deadGuid})
        # Terminal death wins; hurt cancels a queued attack before the old frame-2 marker.
        $actionGraph.transitions = @(
            @{from='*';to='dead';onClipFinished=$false;keepPhase=$false;conditions=@(@{param='dead';op='is_true'});consumeTriggers=@()},
            @{from='*';to='hurt';onClipFinished=$false;keepPhase=$false;conditions=@(@{param='hurt';op='is_true'},@{param='dead';op='is_false'});consumeTriggers=@('attack')},
            @{from='*';to='attack';onClipFinished=$false;keepPhase=$false;conditions=@(@{param='attack';op='is_true'},@{param='dead';op='is_false'});consumeTriggers=@()},
            @{from='attack';to='idle';onClipFinished=$true;keepPhase=$false;conditions=@();consumeTriggers=@()},
            @{from='hurt';to='idle';onClipFinished=$true;keepPhase=$false;conditions=@();consumeTriggers=@()},
            @{from='idle';to='walk';onClipFinished=$false;keepPhase=$false;conditions=@(@{param='moving';op='is_true'});consumeTriggers=@()},
            @{from='walk';to='idle';onClipFinished=$false;keepPhase=$false;conditions=@(@{param='moving';op='is_false'});consumeTriggers=@()})
        Write-Json $statePath $actionGraph
        $actionSource = @'
local mode='_MODE_'
local n,entries,hits=0,0,0
local hp=100
local locked=nil
return {
    on_init=function(self)
        local e=mye.world.entity_from_packed(self.entity)
        assert(e:get_animation_state()=='idle' and mye.controller2d.is_enabled(self.entity))
        mye.log('action-init:'..mode)
    end,
    on_update=function(self,dt)
        n=n+1;local e=mye.world.entity_from_packed(self.entity)
        local state=e:get_animation_state()
        if n==2 then
            locked=e:get_position();e:set_trigger('attack');mye.controller2d.set_enabled(self.entity,false)
        elseif n==3 and mode~='normal' then
            e:set_trigger('attack');e:reset_trigger('attack')
            if mode=='hurt' then hp=75;e:set_trigger('hurt') else hp=0;e:reset_trigger('hurt');e:set_bool('dead',true) end
            mye.controller2d.set_enabled(self.entity,false)
        elseif n>2 and hp>0 then
            mye.controller2d.set_enabled(self.entity,state=='idle' or state=='walk')
        end
        if n>2 and not mye.controller2d.is_enabled(self.entity) then
            local p=e:get_position();local v=e:get_velocity()
            assert(p.x==locked.x and p.y==locked.y and v.x==0 and v.y==0 and not e:get_bool('moving'))
        end
        if n==10 then
            assert(entries==1 and hits==(mode=='normal' and 1 or 0))
            assert(not e:get_bool('attack') and not e:get_bool('hurt'))
            assert((hp==0 and state=='dead' and not mye.controller2d.is_enabled(self.entity)) or
                (hp>0 and (state=='idle' or state=='walk') and mye.controller2d.is_enabled(self.entity)))
            mye.log('action-ready:'..mode..':hits='..hits)
        end
    end,
    on_event=function(self,name,payload)
        if name~='animation' then return end
        local e=mye.world.entity_from_packed(self.entity)
        if payload.name=='action-attack' then entries=entries+1;assert(e:get_animation_state()=='attack') end
        if payload.name=='action-hit' then hits=hits+1;assert(hp>0 and hits==1 and e:get_animation_state()=='attack') end
        mye.log('action-marker:'..mode..':'..payload.name)
    end
}
'@
        try {
            $actionHashes = @((Get-FileHash -LiteralPath $statePath).Hash, (Get-FileHash -LiteralPath $attackPath).Hash)
            foreach ($mode in @('normal','hurt','death')) {
                $player.components.ObjectBehavior.luaSource = $actionSource.Replace('_MODE_', $mode); Write-Json $scenePath $scene
                $actionSceneHash = (Get-FileHash -LiteralPath $scenePath).Hash
                foreach ($entry in @(@($game,'game'), @($editor,'editor'))) {
                    $name = "action-$mode-$($entry[1])"
                    $arguments = @('--project',$project,'--headless') + $(if ($entry[1] -eq 'game') {@('--ticks','40')} else {@('--play','--frames','12000')})
                    $log = Run-App $entry[0] $name ($arguments + @('--dump',(Join-Path $run "$name.bmp")))
                    foreach ($marker in @("action-init:$mode", "action-ready:$mode`:hits=$(if($mode -eq 'normal'){1}else{0})", "action-marker:$mode`:action-attack")) {
                        if (([regex]::Matches($log,[regex]::Escape($marker))).Count -ne 1) {throw "Action marker missing/duplicate: $name $marker"}
                    }
                    $hitCount = ([regex]::Matches($log,"action-marker:$mode`:action-hit")).Count
                    if ($hitCount -ne $(if($mode -eq 'normal'){1}else{0})) {throw "Interrupted hit lifetime failed: $name"}
                    if ($mode -eq 'hurt' -and ([regex]::Matches($log,'action-marker:hurt:action-hurt')).Count -ne 1) {throw 'Missing hurt entry'}
                    if ($mode -eq 'death' -and ([regex]::Matches($log,'action-marker:death:action-dead')).Count -ne 1) {throw 'Missing death entry'}
                    Check-Pixel $name 480 222 $(if($mode -eq 'death'){0xFF8800}else{0x808080})
                }
                if ((Get-FileHash -LiteralPath $scenePath).Hash -ne $actionSceneHash) {throw 'Action playback changed the saved scene'}
            }
            if ((Get-FileHash -LiteralPath $statePath).Hash -ne $actionHashes[0] -or
                (Get-FileHash -LiteralPath $attackPath).Hash -ne $actionHashes[1]) {throw 'Action playback changed its definition/clip'}
        } finally {
            [IO.File]::WriteAllBytes($statePath,$stateBytes); [IO.File]::WriteAllBytes($attackPath,$attackBytes)
            [IO.File]::WriteAllBytes($scenePath,$savedActionScene)
            $scene = Get-Content -LiteralPath $scenePath -Raw -Encoding UTF8 | ConvertFrom-Json; $player = $scene.entities[0]
        }
        Write-Output 'PASS: actual Play/MyGame scripted action selection, frame-2 hit once, hurt/death cancel before hit, trigger reset, current-state reads and local control lock (physical action keys remain a separate gate)'
    }
    if ($StateMaps) {
        $savedScene = [IO.File]::ReadAllBytes($scenePath)
        $mapBPath = Join-Path $run 'project/assets/scenes/state-map-b.scene'
        $badGraphPath = Join-Path $run 'project/assets/animations/state-map-bad.animstate'
        $badGraphGuid = [Guid]::NewGuid().ToString()
        $mapSource = @'
local tag='_MAP_TAG_'
local n,idles,attacks=0,0,0
local returned=false
return {
    on_init=function(self)
        local e=mye.world.entity_from_packed(self.entity)
        assert(StateMapProbe==nil and not e:get_bool('ready') and e:get_float('seed')==0.375)
        local p=e:get_position()
        returned=tag=='a' and p.x==-4 and p.y==1
        assert(returned or (tag=='a' and p.x==0 and p.y==0) or (tag=='b' and p.x==4 and p.y==2))
        StateMapProbe=tag; e:set_bool('ready',true); e:set_float('seed',0.625)
        mye.log('map-init:'..tag..(returned and ':return' or ':first'))
    end,
    on_update=function(self,dt)
        n=n+1;local e=mye.world.entity_from_packed(self.entity)
        assert(StateMapProbe==tag and e:get_bool('ready') and e:get_float('seed')==0.625)
        if not returned then
            if n==2 then e:set_trigger('attack') end
            if n==4 then assert(attacks==1); e:set_position(mye.Vec2(2,0)) end
        elseif n==6 then
            assert(idles==1 and attacks==0);mye.log('map-return-ready')
        end
    end,
    on_event=function(self,name,payload)
        if name~='animation' then return end
        if payload.name=='state-idle' then idles=idles+1 end
        if payload.name=='state-attack' then attacks=attacks+1 end
        assert(attacks<=1);mye.log('map-event:'..tag..(returned and ':return:' or ':first:')..payload.name)
    end,
    on_destroy=function(self) mye.log('map-destroy:'..tag..(returned and ':return' or ':first')) end
}
'@
        $mapA = $scene | ConvertTo-Json -Depth 32 | ConvertFrom-Json
        $mapPlayer = $mapA.entities[0]
        $mapPlayer.components.ObjectBehavior.luaSource = $mapSource.Replace('_MAP_TAG_', 'a')
        $portalPose = $pose | ConvertTo-Json | ConvertFrom-Json
        $portalPose.px = 2; $portalPose.py = 0
        $spawnPose = $pose | ConvertTo-Json | ConvertFrom-Json
        $spawnPose.px = -4; $spawnPose.py = 1
        $mapA.entities += @(
            [pscustomobject]@{id=901;components=@{
                ObjectName=@{value='Portal'};LocalTransform=$portalPose
                Collider2D=@{shape=@{kind='Box';half=@{x=.3;y=.3}};isTrigger=$true}
                ScenePortal=@{scenePath='assets/scenes/state-map-b.scene';spawnName='Arrival';onInteract=$false}}},
            [pscustomobject]@{id=902;components=@{ObjectName=@{value='Arrival'};LocalTransform=$spawnPose}})
        $mapB = $mapA | ConvertTo-Json -Depth 32 | ConvertFrom-Json
        $mapB.entities[0].components.ObjectBehavior.luaSource = $mapSource.Replace('_MAP_TAG_', 'b')
        $mapB.entities[2].components.ScenePortal.scenePath = $manifest.mainScene
        $mapB.entities[3].components.LocalTransform.px = 4; $mapB.entities[3].components.LocalTransform.py = 2
        Write-Json $scenePath $mapA; Write-Json $mapBPath $mapB
        [IO.File]::WriteAllBytes((Join-Path $run 'state-map-a.input.json'),[IO.File]::ReadAllBytes($scenePath))
        [IO.File]::WriteAllBytes((Join-Path $run 'state-map-b.input.json'),[IO.File]::ReadAllBytes($mapBPath))
        $mapHashes = @((Get-FileHash -LiteralPath $scenePath).Hash, (Get-FileHash -LiteralPath $mapBPath).Hash)
        try {
            # ponytail: editor stops by render frames; require the return marker. Use a fixed-tick limit if faster renderers finish too early.
            foreach ($entry in @(@($game,'game'),@($editor,'editor'))) {
                $arguments = @('--project',$project,'--headless') + $(if ($entry[1] -eq 'game') {@('--ticks','80')} else {@('--play','--frames','20000')})
                $log = Run-App $entry[0] "state-maps-$($entry[1])" ($arguments + @('--dump',(Join-Path $run "state-maps-$($entry[1]).bmp"))) -Timeout 45000
                foreach ($marker in @('map-init:a:first','map-init:b:first','map-init:a:return','map-return-ready',
                    'map-event:a:first:state-attack','map-event:b:first:state-attack','map-event:a:return:state-idle',
                    'map-destroy:a:first','map-destroy:b:first','map-destroy:a:return')) {
                    if (([regex]::Matches($log,[regex]::Escape($marker))).Count -ne 1) {throw "Map $($entry[1]) marker failed: $marker"}
                }
                if ($log -match 'map-event:a:return:state-attack') {throw 'Departed trigger leaked into the returned animator'}
                Check-Pixel "state-maps-$($entry[1])" 480 222 0x808080
            }
            if ((Get-FileHash -LiteralPath $scenePath).Hash -ne $mapHashes[0] -or
                (Get-FileHash -LiteralPath $mapBPath).Hash -ne $mapHashes[1]) {throw 'Map playback changed saved scenes'}
            # Only the destination uses this valid graph with a missing inactive clip.
            $badGraph = $graph | ConvertTo-Json -Depth 32 | ConvertFrom-Json
            $badGraph.states[2].animation = [Guid]::NewGuid().ToString()
            Write-Json $badGraphPath $badGraph
            Write-Json ($badGraphPath+'.meta') @{guid=$badGraphGuid;importer='AnimationStateAsset';importerVersion=1}
            $mapB.entities[0].components.SpriteAnimator.stateMachine.guid = $badGraphGuid
            Write-Json $mapBPath $mapB
            foreach ($entry in @(@($game,'game'),@($editor,'editor'))) {
                $arguments = @('--project',$project,'--headless') + $(if ($entry[1] -eq 'game') {@('--ticks','80')} else {@('--play','--frames','20000')})
                $log = Run-App $entry[0] "state-map-refused-$($entry[1])" $arguments 1
                if ($log -notmatch 'map-init:a:first' -or $log -match 'map-init:b:first|map-destroy:b:first' -or
                    $log -notmatch 'assets/scenes/state-map-b\.scene: attack: animation GUID does not identify an \.anim file') {
                    throw 'Broken destination ran Lua or lacked the destination animation diagnostic'
                }
            }
        } finally { [IO.File]::WriteAllBytes($scenePath,$savedScene) }
        Write-Output 'PASS: actual local/Play A-B-A portals, named spawns, fresh Lua/typed parameters/cursors, entry/attack event lifetime, source preservation and broken destination refusal before Lua'
    }
    $player.components.ObjectBehavior.luaSource='return {on_init=function(self) mye.log("client-lua-must-not-run");error("client authority violation") end}'
    Write-Json $scenePath $scene
    $stateData=Join-Path $run 'state-online-data';New-Item -ItemType Directory -Path $stateData | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $stateData 'state.json'),$initialState)
    $owned=@()
    try {
        $hostProcess=Start-App $server 'state-server' @('--data',$stateData,'--project',$project,'--port','0','--ticks','600');$owned+=$hostProcess
        $timer=[Diagnostics.Stopwatch]::StartNew();$port=0
        while($timer.Elapsed.TotalSeconds -lt 5 -and -not $hostProcess.HasExited){
            $log=Get-Content -LiteralPath (Join-Path $run 'state-server.log') -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
            if($log -match 'MyServer .*?port (\d+), tickrate 60Hz'){$port=[int]$Matches[1];break};Start-Sleep -Milliseconds 25
        }
        if(-not $port){throw 'State server readiness failed'}
        # Both applications must be admitted before their movement captures; retain distinct fixed/replay step numbers.
        $bInput=Replay 'state-b' @(@{ticks=120;x=0;y=0},@{ticks=40;x=-1;y=0},@{ticks=180;x=0;y=0});$aInput=Replay 'state-a' @(@{ticks=120;x=0;y=0},@{ticks=40;x=1;y=0},@{ticks=30;x=0;y=0})
        foreach($entry in @(@('state-b','directions-b',$bInput),@('state-a','directions-a',$aInput))){
            $process=Start-App $game $entry[0] @('--project',$project,'--headless','--connect',"127.0.0.1:$port",'--credentials',(Join-Path $run ($entry[1]+'.json')),'--input',$entry[2],'--capture-at','159,160,161,162','--dump',(Join-Path $run ($entry[0]+'.bmp')));$owned+=$process
            if($entry[0] -eq 'state-b'){Start-Sleep -Milliseconds 300}
        }
        $aLog=Wait-App $owned[2] 'state-a';$bLog=Wait-App $owned[1] 'state-b'
        if($aLog -notmatch 'Input replay confirmed: steps=190, pending=0' -or $bLog -notmatch 'Input replay confirmed: steps=340, pending=0'){throw 'State movement acknowledgment failed'}
        foreach($name in @('state-a','state-b')){foreach($step in @(159,160,161,162)){
            $capture=Get-Content -LiteralPath (Join-Path $run "$name.step-$step.json") -Raw -Encoding UTF8 | ConvertFrom-Json
            $local=@($capture.actors | Where-Object local);$remote=@($capture.actors | Where-Object {-not $_.local})
            if($local.Count -ne 1 -or $remote.Count -ne 1 -or $local[0].animationState -ne $(if($step -le 160){'walk'}else{'idle'})){throw "Online $name step $step state failed"}
            foreach($actor in $capture.actors){
                if($actor.animationState -notin @('idle','walk')){throw 'Online action state outside movement presentation'}
                $colour=if($actor.animationState -eq 'idle'){0x808080}else{[Convert]::ToInt32($colours[$actor.facing],16)}
                Check-Pixel "$name.step-$step" ([int][Math]::Round($actor.screenX)) ([int][Math]::Round($actor.screenY)-48) $colour
            }
        }}
        $null=Wait-App $hostProcess 'state-server'
    } finally {foreach($process in $owned){if(-not $process.HasExited){$process.Kill();$process.WaitForExit()};$process.Dispose()}}
    if((Get-FileHash -LiteralPath $statePath).Hash -ne $savedGraphHash -or (Get-FileHash -LiteralPath ($statePath+'.meta')).Hash -ne $savedStateMetaHash){throw 'State data/GUID changed'}
    $player.components.PSObject.Properties.Remove('ObjectBehavior');$player.components.SpriteAnimator.PSObject.Properties.Remove('stateMachine');$player.components.SpriteAnimator.__version=1;Write-Json $scenePath $scene
    Write-Output 'PASS: saved states, typed defaults before Lua init, different sheets, entry/trigger/completion, Play/open clip, inactive asset refusal and two admitted clients with Lua isolation'
}

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
    if ($Temporal) {
        $continuous = @(
            @{ticks=9;x=0;y=-1;animationPlaying=$true},@{ticks=4;x=-1;y=0;animationPlaying=$false},
            @{ticks=4;x=1;y=0;animationPlaying=$false},@{ticks=5;x=0;y=1;animationPlaying=$true},
            @{ticks=5;x=1;y=1;animationPlaying=$true},@{ticks=5;x=0;y=-1;animationPlaying=$true})
        $captureAt = (1..32) -join ','
        $inputFile = Replay 'continuous-local' $continuous
        $continuousLog = Run-App $game 'continuous-local' @('--project',$project,'--headless','--input',$inputFile,
            '--capture-at',$captureAt,'--dump',(Join-Path $run 'continuous-local.bmp'))
        function Clip-Layout([int]$Facing) {
            if ($Facing -in @(2,6)) { return @{frames=@(4,5);seconds=@(.125,.375)} }
            if ($Facing -eq 4) { return @{frames=@(7,6,5);seconds=@(.25,.125,.125)} }
            if ($Facing -eq 5) { return @{frames=@(2,4,6,4);seconds=@(.125,.25,.125,.25)} }
            return @{frames=@(0,1,2,3);seconds=@(.125,.125,.25,.5)}
        }
        function Check-Continuous([string]$Name,[int]$First,[int]$Last,[bool]$Online) {
            $previous=@{}; $changes=0; $remoteChanges=0; $paused=0; $resumed=0; $remoteFrames=0; $remoteMovingPaused=0; $remoteFacings=@{}
            for ($step=$First; $step -le $Last; $step++) {
                $sample=Get-Content -LiteralPath (Join-Path $run "$Name.step-$step.json") -Raw -Encoding UTF8 | ConvertFrom-Json
                if ($sample.version -ne 1 -or $sample.replayStep -ne $step) { throw "Capture boundary mismatch: $Name $step" }
                $local=@($sample.actors | Where-Object { $_.local -or -not $Online })
                if ($local.Count -ne 1) { throw "Expected one local actor: $Name $step" }
                $relative=$step-$First+1
                if ($local[0].playing -ne ($relative -lt 10 -or $relative -ge 18)) { throw "Pause diagnostic mismatch: $Name $step" }
                foreach ($actor in $sample.actors) {
                    if ($actor.finished) { throw "Temporal check used a completed motion: $Name $step" }
                    $key=[string]$actor.netId; $layout=Clip-Layout $actor.facing
                    $total=($layout.seconds | Measure-Object -Sum).Sum
                    if ($previous.ContainsKey($key)) {
                        $old=$previous[$key]; $oldLayout=Clip-Layout $old.actor.facing
                        if ($sample.fixedTick-$old.tick -ne 1) { throw "Nonconsecutive captured fixed ticks: $Name $step" }
                        $elapsed=$old.actor.cursorSeconds
                        for ($i=0; $i -lt $old.actor.cursorStep; $i++) { $elapsed+=$oldLayout.seconds[$i] }
                        $fraction=$elapsed/($oldLayout.seconds | Measure-Object -Sum).Sum
                        $elapsed=$fraction*$total
                        if ($actor.playing) { $elapsed+=($sample.fixedTick-$old.tick)/60.0 }
                        $elapsed=$elapsed%$total; $index=0
                        while ($index+1 -lt $layout.seconds.Count -and $elapsed -ge $layout.seconds[$index]) { $elapsed-=$layout.seconds[$index]; $index++ }
                        if ($actor.cursorStep -ne $index -or [math]::Abs($actor.cursorSeconds-$elapsed) -gt .00002 -or $actor.frame -ne $layout.frames[$index]) {
                            throw "Phase/time mismatch: $Name step=$step peer=$key"
                        }
                        if ($actor.facing -ne $old.actor.facing) { $changes++; if (-not $actor.local -and $Online) { $remoteChanges++ } }
                        if (-not $actor.playing) {
                            $paused++
                            $dx=$actor.worldX-$old.actor.worldX; $dy=$actor.worldY-$old.actor.worldY
                            if ($actor.local -or -not $Online) {
                                $expectedDx=$(if ($relative -lt 14) { -.05 } else { .05 })
                                if ([math]::Abs($dx-$expectedDx) -gt .00005 -or [math]::Abs($dy) -gt .00005) { throw "Animation pause stopped character movement: $Name $step" }
                            } elseif ([math]::Abs($dx)+[math]::Abs($dy) -gt .00001) { $remoteMovingPaused++ }
                        }
                        if ($actor.playing -and -not $old.actor.playing) { $resumed++ }
                    }
                    if ($actor.flipX -ne ($actor.facing -eq 6)) { throw "Mirrored facing mismatch: $Name $step" }
                    $colour=[Convert]::ToInt32($phaseColours[$actor.frame],16)
                    $x=[int][math]::Floor($actor.screenX); $y=[int][math]::Floor($actor.screenY)
                    Check-Pixel "$Name.step-$step" ($x+10) ($y-16) $colour
                    Check-Pixel "$Name.step-$step" $x ($y-1) 0xFFFFFF
                    if (-not $actor.local -and $Online) { $remoteFrames++; $remoteFacings[[int]$actor.facing]=$true }
                    $previous[$key]=@{actor=$actor;tick=$sample.fixedTick}
                }
            }
            if ($Online) {
                foreach ($facing in @(0,2,4,5,6)) { if (-not $remoteFacings.ContainsKey($facing)) { throw "Missing remote facing $facing : $Name" } }
            }
            if ($changes -lt 5 -or $paused -lt 8 -or $resumed -lt 1 -or ($Online -and ($remoteChanges -lt 1 -or $remoteFrames -lt 4 -or $remoteMovingPaused -lt 1))) {
                throw "Incomplete continuous coverage: $Name changes=$changes remoteChanges=$remoteChanges paused=$paused resumed=$resumed remoteFrames=$remoteFrames"
            }
            Write-Output "PASS: $Name 32 exact input boundaries, incomplete-motion phase/pixels/pause/resume; remote changes=$remoteChanges frames=$remoteFrames paused authority moves=$remoteMovingPaused"
        }
        Check-Continuous 'continuous-local' 1 32 $false
        if ([regex]::Matches($continuousLog,'phase-event:phase-start').Count -ne 1 -or
            $continuousLog -match 'phase-event:phase-left-(entry|step)') { throw 'Continuous captures consumed or duplicated fixed-tick events' }
        # Refuse invalid lists/diagnostic types and failed BMP/atomic JSON output.
        foreach ($value in @('0','36001','18446744073709551616','2,1','1,1','-1','1,','1.5',((1..33)-join ','))) {
            $null=Run-App $game ('bad-capture-'+[Guid]::NewGuid().ToString('N')) @('--project',$project,'--input',$inputFile,'--capture-at',$value,'--dump',(Join-Path $run 'bad.bmp')) 64
        }
        $null=Run-App $game 'missing-capture-input' @('--project',$project,'--capture-at','1','--dump',(Join-Path $run 'bad.bmp')) 64
        $null=Run-App $game 'missing-capture-dump' @('--project',$project,'--capture-at','1','--input',$inputFile) 64
        $null=Run-App $game 'long-capture' @('--project',$project,'--headless','--input',$inputFile,'--capture-at','33','--dump',(Join-Path $run 'bad.bmp')) 1
        $badInput=Replay 'bad-playing' @(@{ticks=1;x=0;y=0;animationPlaying='false'})
        $badNullInput=Replay 'bad-playing-null' @(@{ticks=1;x=0;y=0;animationPlaying=$null})
        $null=Run-App $game 'bad-playing-null' @('--project',$project,'--headless','--input',$badNullInput) 1
        $null=Run-App $game 'bad-playing' @('--project',$project,'--headless','--input',$badInput) 1
        $null=Run-App $game 'capture-bmp-failure' @('--project',$project,'--headless','--input',$inputFile,'--capture-at','1','--dump',(Join-Path $run 'missing/output.bmp')) 1
        New-Item -ItemType Directory -Path (Join-Path $run 'blocked.step-1.json') | Out-Null
        $null=Run-App $game 'capture-json-failure' @('--project',$project,'--headless','--input',$inputFile,'--capture-at','1','--dump',(Join-Path $run 'blocked.bmp')) 1
        $caseData=Join-Path $run 'continuous-data'; New-Item -ItemType Directory -Path $caseData | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $caseData 'state.json'),$initialState)
        $player.components.ObjectBehavior.luaSource='return { on_init=function(self) mye.log("client-lua-must-not-run"); error("client authority violation") end }'
        Write-Json $scenePath $scene
        $owned=@()
        try {
            $hostProcess=Start-App $server 'continuous-server' @('--data',$caseData,'--project',$project,'--port','0','--ticks','600');$owned+=$hostProcess
            $timer=[Diagnostics.Stopwatch]::StartNew();$port=0
            while ($timer.Elapsed.TotalSeconds -lt 5 -and -not $hostProcess.HasExited) {
                $log=Get-Content -LiteralPath (Join-Path $run 'continuous-server.log') -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
                if ($log -match 'MyServer .*?port (\d+), tickrate 60Hz') { $port=[int]$Matches[1];break }
                Start-Sleep -Milliseconds 25
            }
            if (-not $port) { throw 'Continuous server readiness failed' }
            $bInput=Replay 'continuous-b' (@(@{ticks=40;x=-1;y=0})+$continuous+@(@{ticks=180;x=0;y=0}))
            $aInput=Replay 'continuous-a' (@(@{ticks=40;x=1;y=0})+$continuous)
            $b=Start-App $game 'continuous-b' @('--project',$project,'--headless','--connect',"127.0.0.1:$port",'--credentials',(Join-Path $run 'directions-b.json'),'--input',$bInput,'--capture-at',((41..72)-join ','),'--dump',(Join-Path $run 'continuous-b.bmp'));$owned+=$b
            $a=Start-App $game 'continuous-a' @('--project',$project,'--headless','--connect',"127.0.0.1:$port",'--credentials',(Join-Path $run 'directions-a.json'),'--input',$aInput,'--capture-at',((41..72)-join ','),'--dump',(Join-Path $run 'continuous-a.bmp'));$owned+=$a
            $aLog=Wait-App $a 'continuous-a';$bLog=Wait-App $b 'continuous-b'
            if ($aLog -notmatch 'Input replay confirmed: steps=72, pending=0' -or $bLog -notmatch 'Input replay confirmed: steps=252, pending=0') { throw 'Continuous authority acknowledgment failed' }
            Check-Continuous 'continuous-a' 41 72 $true;Check-Continuous 'continuous-b' 41 72 $true
            $null=Wait-App $hostProcess 'continuous-server'
        } finally { foreach ($process in $owned) { if (-not $process.HasExited) { $process.Kill();$process.WaitForExit() };$process.Dispose() } }
        $player.components.ObjectBehavior.luaSource='return { on_event=function(self, name, payload) if name == "animation" then mye.log("phase-event:" .. payload.name) end end }'
        Write-Json $scenePath $scene
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
    Write-Output ('PASS: normalized unequal/reverse/ping-pong phase, silent remap, asymmetric feet, completed Play/online endpoints' +
        $(if ($Temporal) { '; continuous partial local/online capture sequences passed' } else { ' (use -Temporal for continuous partial sequences)' }))
}
if ((Get-FileHash -LiteralPath $scenePath).Hash -ne $sceneHash -or
    (Get-FileHash -LiteralPath ($animationPath + '.meta')).Hash -ne $metaHash) { throw 'Authored scene/GUID changed' }
Write-Output "PASS: $Configuration saved 8-facing clips, direct/mirror/default pixels, editor preview/Play, strict rejection, two authoritative clients/Lua isolation ($run)"
exit 0
