param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$BuildDir = 'build/dev'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repo $BuildDir }
$serverExe = Join-Path $build "apps/server/$Configuration/MyServer.exe"
$gameExe = Join-Path $build "apps/game/$Configuration/MyGame.exe"
$run = Join-Path $repo ('build/online-world/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'game/starter/meadow_village') -Destination (Join-Path $run 'project') -Recurse
$project = Join-Path $run 'project/project.myeproj'
$manifest = Get-Content $project -Raw -Encoding UTF8 | ConvertFrom-Json
function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
}
$manifest | Add-Member onlineCombat ([pscustomobject]@{ range = 2; power = 1; cooldownTicks = 60 }) -Force
Write-Json $project $manifest
$sceneA = Join-Path (Split-Path -Parent $project) $manifest.mainScene
$sceneBRelative = 'assets/scenes/online-other.scene'
$sceneB = Join-Path (Split-Path -Parent $project) $sceneBRelative
$scene = Get-Content $sceneA -Raw -Encoding UTF8 | ConvertFrom-Json
$player = @($scene.entities | Where-Object { $_.components.CharacterController2D.enabled })[0]
$player.components.LocalTransform.px = 0; $player.components.LocalTransform.py = 0
$player.components.FloorLevel.level = 0
$player.components.Collider2D.shape.kind = 'Circle'
$player.components.Collider2D.shape.half.x = .1; $player.components.Collider2D.shape.half.y = .1
$player.components.Collider2D.offset.x = 0; $player.components.Collider2D.offset.y = 0
$player.components.PSObject.Properties.Remove('ObjectBehavior')
function Marker([int]$Id, [string]$Name, [double]$X, [int]$Floor) {
    $pose = $player.components.LocalTransform | ConvertTo-Json | ConvertFrom-Json
    $pose.px = $X; $pose.py = 0; $pose.sx = 1; $pose.sy = 1
    return [pscustomobject]@{ id=$Id; components=[pscustomobject]@{
        ObjectName=@{value=$Name}; LocalTransform=$pose; FloorLevel=@{level=$Floor}
    } }
}
$gate = Marker 800 'Gate' 0 0
$gate.components | Add-Member ScenePortal ([pscustomobject]@{ scenePath=$sceneBRelative; spawnName='Arrival'; onInteract=$true })
$gate.components | Add-Member InteractionTarget ([pscustomobject]@{ enabled=$true; radius=1; prompt='Travel'; eventName='interact' })
$scene.entities = @($player, $gate, (Marker 801 'Arrival' 0 0))
Write-Json $sceneA $scene
$other = $scene | ConvertTo-Json -Depth 32 | ConvertFrom-Json
$other.entities[0].components.FloorLevel.level = 1
$other.entities[1].components.ScenePortal.scenePath = $manifest.mainScene
$other.entities[1].components.LocalTransform.px = 2
$other.entities[1].components.FloorLevel.level = 1
$other.entities[2].components.LocalTransform.px = 2
$other.entities[2].components.FloorLevel.level = 1
Write-Json $sceneB $other
$authoredHashes = @((Get-FileHash $sceneA).Hash, (Get-FileHash $sceneB).Hash)
$data = Join-Path $run 'data'
function Invoke-Check([string]$Exe, [string]$Name, [int]$Expected, [string[]]$Arguments) {
    $log = Join-Path $run "$Name.log"
    $previous = $ErrorActionPreference
    try { $ErrorActionPreference='Continue'; & $Exe @Arguments > $log 2>&1; $actual=$LASTEXITCODE }
    finally { $ErrorActionPreference=$previous }
    if ($actual -ne $Expected) { throw "$Name expected $Expected, got $actual ($run)" }
}
foreach ($user in @('world-a','world-b')) {
    Invoke-Check $serverExe "$user-register" 0 @('--data',$data,'--register',$user,'local-test-only')
    Invoke-Check $serverExe "$user-character" 0 @('--data',$data,'--make-char',$user,$user)
    Write-Json (Join-Path $run "$user.json") @{username=$user;password='local-test-only'}
}
function Replay([string]$Name, [object[]]$Steps) {
    $path = Join-Path $run "$Name-input.json"; Write-Json $path @{version=1;steps=$Steps}; return $path
}
$hold = Replay 'hold' @(@{ticks=900;x=0;y=0})
$roundtrip = Replay 'roundtrip' @(@{ticks=600;x=0;y=0}, @{ticks=1;x=0;y=0;attackTarget=2},
    @{ticks=1;x=0;y=0;attackTarget=2}, @{ticks=60;x=0;y=0}, @{ticks=1;x=0;y=0;interact=$true},
    @{ticks=60;x=0;y=0}, @{ticks=1;x=0;y=0;interact=$true}, @{ticks=60;x=0;y=0})
$travel = Replay 'travel' @(@{ticks=30;x=0;y=0}, @{ticks=1;x=0;y=0;interact=$true}, @{ticks=30;x=0;y=0})
$idle = Replay 'idle' @(@{ticks=30;x=0;y=0})
$owned = @()
function Start-App([string]$Exe, [string]$Name, [string[]]$Arguments) {
    $quoted = @($Arguments | ForEach-Object { '"' + $_ + '"' })
    $process = Start-Process $Exe -ArgumentList $quoted -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $run "$Name.log") -RedirectStandardError (Join-Path $run "$Name.err")
    $null = $process.Handle
    $script:owned += $process
    return $process
}
function Wait-App($Process, [string]$Name, [int]$Timeout=45000) {
    if (-not $Process.WaitForExit($Timeout) -or $Process.ExitCode -ne 0) { throw "$Name failed ($run)" }
    $log = Get-Content (Join-Path $run "$Name.log") -Raw -Encoding UTF8
    if ($log -match '\[ERROR\]') { throw "$Name reported error ($run)" }
    return $log
}
function Start-Server([string]$Name, [int]$Ticks) {
    $process = Start-App $serverExe $Name @('--data',$data,'--project',$project,'--port','0','--ticks',"$Ticks")
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt 10 -and -not $process.HasExited) {
        $log = Get-Content (Join-Path $run "$Name.log") -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
        if ($log -match 'MyServer .*?port (\d+), tickrate 60Hz') { return @{Process=$process;Port=[int]$Matches[1]} }
        Start-Sleep -Milliseconds 25
    }
    throw "Server startup failed ($run)"
}
function Start-Player([string]$Name, [string]$User, [string]$InputFile, [int]$Port, [string]$Scene='') {
    $arguments = @('--project',$project,'--headless','--connect',"127.0.0.1:$Port",'--credentials',
        (Join-Path $run "$User.json"),'--input',$InputFile,'--dump',(Join-Path $run "$Name.bmp"))
    if ($Scene) { $arguments += @('--scene',$Scene) }
    return Start-App $gameExe $Name $arguments
}
try {
    $server = Start-Server 'server' 2400
    $a = Start-Player 'roundtrip' 'world-a' $roundtrip $server.Port
    $admission = [Diagnostics.Stopwatch]::StartNew()
    while ($admission.Elapsed.TotalSeconds -lt 10 -and -not $a.HasExited) {
        $ready = Get-Content (Join-Path $run 'roundtrip.log') -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
        if ($ready -match 'Online spawn confirmed: netId=1,') { break }
        Start-Sleep -Milliseconds 25
    }
    if ($ready -notmatch 'Online spawn confirmed: netId=1,') { throw 'First player admission did not precede the peer' }
    $b = Start-Player 'hold' 'world-b' $hold $server.Port
    $log = Wait-App $a 'roundtrip'
    if ($log -notmatch 'Attack confirmed: .*accepted=true, damage=[1-9]' -or
        @([regex]::Matches($log,'Online map entered:')).Count -ne 2 -or
        $log -notmatch 'epoch=2, position=\(0, 0\)' -or $log -notmatch 'Input replay confirmed:') {
        throw "Authority attack/map roundtrip was not observed ($run)"
    }
    $a = Start-Player 'travel' 'world-a' $travel $server.Port
    $log = Wait-App $a 'travel'
    if ($log -notmatch 'Online map entered: .*epoch=1, position=\(2, 0\)') { throw 'Arrival was not committed' }
    $bLog = Wait-App $b 'hold'
    $health = @([regex]::Matches($bLog,'Online health confirmed: netId=2, hp=(\d+), maxHp=(\d+)'))
    if ($health.Count -lt 2 -or [int]$health[-1].Groups[1].Value -ge [int]$health[0].Groups[1].Value -or
        @([regex]::Matches($bLog,'Network object removed:')).Count -lt 2) { throw 'Peer HP/map removal was not replicated' }
    $null = Wait-App $server.Process 'server'
    $saved = Get-Content (Join-Path $data 'state.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $recordA = $saved.characters.records | Where-Object name -eq 'world-a'
    $recordB = $saved.characters.records | Where-Object name -eq 'world-b'
    if ($recordA.sceneId -ne $sceneBRelative -or $recordA.floorLevel -ne 1 -or $recordA.posX -ne 2 -or
        $recordB.hp -le 0 -or $recordB.hp -ge $recordA.hp) { throw 'Map/HP persistence did not match authority' }
    $server = Start-Server 'restart' 240
    $a = Start-Player 'rejoined' 'world-a' $idle $server.Port $sceneBRelative
    $log = Wait-App $a 'rejoined'
    if ($log -notmatch 'Online spawn confirmed: .*position=\(2, 0\), floor=1') { throw 'Saved destination was not restored' }
    $null = Wait-App $server.Process 'restart'
    if ((Get-FileHash $sceneA).Hash -ne $authoredHashes[0] -or (Get-FileHash $sceneB).Hash -ne $authoredHashes[1]) {
        throw 'Playback changed authored scenes'
    }
    Write-Json (Join-Path $run 'result.json') @{passed=$true;roundtrip=$true;restart=$true;hp=$recordB.hp;scene=$recordA.sceneId;floor=$recordA.floorLevel}
    Write-Host "Online world checks passed: $run"
} finally {
    foreach ($process in $owned) { if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force } }
}
exit 0
