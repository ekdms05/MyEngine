param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$BuildDir = 'build/dev',
    [switch]$SavedCamera
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$serverExe = Join-Path $buildRoot "apps/server/$Configuration/MyServer.exe"
$gameExe = Join-Path $buildRoot "apps/game/$Configuration/MyGame.exe"
foreach ($exe in @($serverExe, $gameExe)) {
    if (-not (Test-Path -LiteralPath $exe)) { throw "Build the apps first: $exe" }
}
$runDir = Join-Path $repoRoot ('build/online2d/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'game/starter/meadow_village') -Destination (Join-Path $runDir 'project') -Recurse
$project = Join-Path $runDir 'project/project.myeproj'
$manifest = Get-Content -LiteralPath $project -Raw -Encoding UTF8 | ConvertFrom-Json
$sceneFile = Join-Path (Split-Path -Parent $project) $manifest.mainScene
$authored = Get-Content -LiteralPath $sceneFile -Raw -Encoding UTF8 | ConvertFrom-Json
$player = $authored.entities | Where-Object { $_.components.CharacterController2D.enabled }
if (@($player).Count -ne 1) { throw 'Starter needs one character prototype' }
$player.components.LocalTransform.px = 0
$player.components.LocalTransform.py = 0
$player.components.FloorLevel.level = 1
$player.components.Collider2D.shape.kind = 'Circle'
$player.components.Collider2D.shape.half.x = 0.2
$player.components.Collider2D.shape.half.y = 0.2
$player.components.Collider2D.offset.x = 0.05
$player.components.Collider2D.offset.y = 0.1
$player.components.CharacterController2D.speed = 3
# A client must not instantiate Lua or execute local events in the authoritative scene.
$player.components | Add-Member -NotePropertyName ObjectBehavior -NotePropertyValue ([pscustomobject]@{
    connections = @(); luaSource = "return { on_init = function(self) mye.log('client-lua-must-not-run'); error('client authority violation') end }"
}) -Force
function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
}
function Obstacle([int]$Id, [string]$Name, [double]$X, [int]$Floor, [bool]$Trigger) {
    $pose = $player.components.LocalTransform | ConvertTo-Json | ConvertFrom-Json
    $pose.px = $X; $pose.py = 0; $pose.sx = 1; $pose.sy = 1
    $collider = $player.components.Collider2D | ConvertTo-Json -Depth 8 | ConvertFrom-Json
    $collider.shape.kind = 'Box'; $collider.shape.half.x = 0.005; $collider.shape.half.y = 10
    $collider.offset.x = 0; $collider.offset.y = 0; $collider.isTrigger = $Trigger
    return [pscustomobject]@{ id = $Id; components = [pscustomobject]@{
        ObjectName = [pscustomobject]@{ __version = 1; value = $Name }
        LocalTransform = $pose; Collider2D = $collider
        FloorLevel = [pscustomobject]@{ __version = 1; level = $Floor }
    } }
}
$authored.entities = @($player, (Obstacle 800 'Thin wall' 1.6 1 $false),
    (Obstacle 801 'Other floor' 0 2 $false), (Obstacle 802 'Trigger at spawn' 0 1 $true))
if ($SavedCamera) {
    $player.components | Add-Member -NotePropertyName ObjectName -NotePropertyValue ([pscustomobject]@{ value = 'Local player' }) -Force
    $pose = $player.components.LocalTransform | ConvertTo-Json | ConvertFrom-Json
    $pose.px = 0; $pose.py = 0; $pose.sx = 1; $pose.sy = 1
    $authored.entities += [pscustomobject]@{ id = 803; components = [pscustomobject]@{
        ObjectName = [pscustomobject]@{ value = 'Camera' }; LocalTransform = $pose
        Camera2D = [pscustomobject]@{ __version = 1; current = $true; followTarget = 'Local player'
            zoom = 2; deadzoneHalf = [pscustomobject]@{ x = 0; y = 0 } }
    } }
}
Write-Json $sceneFile $authored
$sceneHash = (Get-FileHash -LiteralPath $sceneFile).Hash
$data = Join-Path $runDir 'data'
$script:step = 0
function Invoke-App([string]$Exe, [int]$Expected, [string[]]$Arguments) {
    $script:step++
    $log = Join-Path $runDir "step-$script:step.log"
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue' # Expected native stderr is not a PowerShell terminating error.
        & $Exe @Arguments > $log 2>&1
        $actual = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    if ($actual -ne $Expected) { throw "Expected exit $Expected, received $actual; $log" }
    return $log
}
foreach ($user in @('online-a', 'online-b')) {
    $null = Invoke-App $serverExe 0 @('--data', $data, '--register', $user, 'local-test-only')
    $null = Invoke-App $serverExe 0 @('--data', $data, '--make-char', $user, $user)
    Write-Json (Join-Path $runDir "$user.json") ([pscustomobject]@{ username = $user; password = 'local-test-only' })
}
# A rendered frame must not turn an unconfirmed online connection into a successful run.
$silentPeer = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
try {
    $port = $silentPeer.Client.LocalEndPoint.Port
    $shortLog = Invoke-App $gameExe 1 @('--project', $project, '--headless', '--connect', "127.0.0.1:$port",
        '--credentials', (Join-Path $runDir 'online-a.json'), '--frames', '1')
    if ((Get-Content -LiteralPath $shortLog -Raw -Encoding UTF8) -notmatch 'Game ended before online admission') {
        throw 'Premature online limit did not report unconfirmed admission'
    }
} finally { $silentPeer.Dispose() }
foreach ($steps in @(@{ ticks = 0; x = 0; y = 0 }, @{ ticks = 1.5; x = 0; y = 0 },
    @{ ticks = 1; x = 2; y = 0 }, @{ ticks = 36001; x = 0; y = 0 }, @{ ticks = 1; x = 0; y = 0; jump = 'bad' })) {
    $inputFile = Join-Path $runDir 'invalid-input.json'
    Write-Json $inputFile ([pscustomobject]@{ version = 1; steps = @($steps) })
    $null = Invoke-App $gameExe 1 @('--project', $project, '--headless', '--input', $inputFile)
}
function Replay([string]$Name, [object[]]$Steps) {
    $path = Join-Path $runDir "$Name-input.json"
    Write-Json $path ([pscustomobject]@{ version = 1; steps = $Steps })
    return $path
}
$aInput = Replay 'a' @(@{ ticks = 60; x = 1; y = 0 }, @{ ticks = 30; x = 0; y = 0 })
if ($SavedCamera) {
    # The first wheel delta is consumed once after admission, never once per waiting tick.
    $aInput = Replay 'a' @(@{ ticks = 1; x = 1; y = 0; cameraZoomSteps = 1 },
        @{ ticks = 59; x = 1; y = 0 }, @{ ticks = 30; x = 0; y = 0 })
}
$bInput = Replay 'b' @(@{ ticks = 45; x = -1; y = 0 }, @{ ticks = 210; x = 0; y = 0 })
$idleInput = Replay 'idle' @(@{ ticks = 20; x = 0; y = 0 })
# Check local replay and its early-limit error through the same fixed-tick input path.
$sceneBytes = [IO.File]::ReadAllBytes($sceneFile)
try {
    $player.components.PSObject.Properties.Remove('ObjectBehavior')
    Write-Json $sceneFile $authored
    $localLog = Invoke-App $gameExe 0 @('--project', $project, '--headless', '--input', $idleInput, '--dump', (Join-Path $runDir 'local.bmp'))
    if ((Get-Content -LiteralPath $localLog -Raw -Encoding UTF8) -notmatch 'Input replay completed: steps=20' -or
        -not (Test-Path -LiteralPath (Join-Path $runDir 'local.bmp'))) { throw 'Local replay/final capture failed' }
    $null = Invoke-App $gameExe 1 @('--project', $project, '--headless', '--input', $idleInput, '--ticks', '1')
} finally { [IO.File]::WriteAllBytes($sceneFile, $sceneBytes) }
foreach ($invalid in @([pscustomobject]@{ version = 2; steps = @(@{ ticks = 1; x = 0; y = 0 }) },
    [pscustomobject]@{ version = 1; steps = @() },
    [pscustomobject]@{ version = 1; steps = @(@{ ticks = 18001; x = 0; y = 0 }, @{ ticks = 18000; x = 0; y = 0 }) })) {
    Write-Json (Join-Path $runDir 'invalid-input.json') $invalid
    $null = Invoke-App $gameExe 1 @('--project', $project, '--headless', '--input', (Join-Path $runDir 'invalid-input.json'))
}
function Start-App([string]$Exe, [string]$Name, [string[]]$Arguments) {
    $quoted = @($Arguments | ForEach-Object { '"' + $_ + '"' })
    $process = Start-Process -FilePath $Exe -ArgumentList $quoted -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $runDir "$Name.log") -RedirectStandardError (Join-Path $runDir "$Name.err")
    $null = $process.Handle # Retain the owned handle so ExitCode remains available after HasExited.
    return $process
}
function Start-Server([string]$Name, [int]$Ticks) {
    $process = Start-App $serverExe $Name @('--data', $data, '--project', $project, '--port', '0', '--ticks', "$Ticks")
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt 5 -and -not $process.HasExited) {
        $log = Get-Content -LiteralPath (Join-Path $runDir "$Name.log") -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
        if ($log -match 'MyServer .*?port (\d+), tickrate 60Hz') {
            return [pscustomobject]@{ Process = $process; Port = [int]$Matches[1] }
        }
        Start-Sleep -Milliseconds 25
    }
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    throw "Server readiness failed: $runDir"
}
function Start-Player([string]$Name, [string]$User, [string]$InputFile, [int]$Port) {
    return Start-App $gameExe $Name @('--project', $project, '--headless', '--connect', "127.0.0.1:$Port",
        '--credentials', (Join-Path $runDir "$User.json"), '--input', $InputFile,
        '--dump', (Join-Path $runDir "$Name.bmp"))
}
function Wait-App($Process, [string]$Name, [int]$Timeout = 15000) {
    if (-not $Process.WaitForExit($Timeout)) { throw "Process did not finish: $Name ($runDir)" }
    if ($Process.ExitCode -ne 0) { throw "Process $Name exited $($Process.ExitCode): $runDir" }
    $log = Get-Content -LiteralPath (Join-Path $runDir "$Name.log") -Raw -Encoding UTF8
    if ($log -match '\[ERROR\]|client-lua-must-not-run') { throw "Unexpected local Lua/error in $Name" }
    return $log
}
function Check-Sprite([string]$Name, [int]$Left, [int]$Right, [bool]$Present) {
    Add-Type -AssemblyName System.Drawing
    $bitmap = [Drawing.Bitmap]::new((Join-Path $runDir "$Name.bmp"))
    try {
        if ($bitmap.Width -ne 960 -or $bitmap.Height -ne 540) { throw 'Incorrect pixel target' }
        $background = $bitmap.GetPixel(0, 0).ToArgb()
        $pixels = 0
        for ($y = 180; $y -lt 280; $y++) {
            for ($x = $Left; $x -lt $Right; $x++) {
                if ($bitmap.GetPixel($x, $y).ToArgb() -ne $background) { $pixels++ }
            }
        }
        if (($Present -and $pixels -lt 100) -or (-not $Present -and $pixels -ne 0)) {
            throw "Sprite visibility mismatch in $Name region $Left..$Right : $pixels"
        }
    } finally { $bitmap.Dispose() }
}
$owned = @()
try {
    $server = Start-Server 'server' 540
    $owned += $server.Process
    $b = Start-Player 'b' 'online-b' $bInput $server.Port
    $owned += $b
    Start-Sleep -Milliseconds 350
    $a = Start-Player 'a' 'online-a' $aInput $server.Port
    $owned += $a
    $aLog = Wait-App $a 'a'
    $bLog = Wait-App $b 'b'
    if ($aLog -notmatch 'dimension=2D' -or $aLog -notmatch 'Online state confirmed: ack=90,' -or
        $aLog -notmatch 'Input replay confirmed: steps=90, pending=0' -or $aLog -notmatch 'Network object added:' -or
        $bLog -notmatch 'Input replay confirmed: steps=255, pending=0' -or $bLog -notmatch 'Network object added:' -or
        $bLog -notmatch 'Network object removed:') { throw "Authority, acknowledgments or peer lifecycle failed: $runDir" }
    if ($aLog -notmatch 'Online state confirmed: ack=90, position=\(([^,]+), ([^)]+)\), floor=1') { throw 'Missing confirmed authority coordinates' }
    $confirmedX = [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
    $confirmedY = [double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture)
    if ([Math]::Abs($confirmedX - 1.345) -gt 0.00001 -or $confirmedY -ne 0) { throw 'Incorrect thin-wall contact' }
    if ($SavedCamera) {
        if ($aLog -notmatch 'Camera2D final: zoom=([^,]+),') { throw 'Missing final camera state' }
        $zoom = [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
        if ([Math]::Abs($zoom - 2.2) -gt 0.00001) { throw "First replay zoom was repeated before admission: $zoom" }
        # Each camera follows its own player; A consumes one wheel step from 2x to 2.2x.
        Check-Sprite 'a' 448 520 $true
        Check-Sprite 'a' 100 175 $true
        Check-Sprite 'b' 448 520 $true
        Check-Sprite 'b' 790 865 $false
    } else {
        Check-Sprite 'a' 520 575 $true # Local player stopped at the offset circle's thin-wall contact.
        Check-Sprite 'a' 345 400 $true # Remote SpriteRenderer is actually rendered, not just counted.
        Check-Sprite 'b' 345 400 $true
        Check-Sprite 'b' 520 575 $false # Remote visual is removed after the other client disconnects.
    }
    $stateFile = Join-Path $data 'state.json'
    $registered = Get-Content -LiteralPath $stateFile -Raw -Encoding UTF8 | ConvertFrom-Json
    $foreign = $registered.characters.characters | Where-Object { $_.name -eq 'online-a' }
    $deniedLog = Invoke-App $gameExe 1 @('--project', $project, '--headless', '--connect', "127.0.0.1:$($server.Port)",
        '--credentials', (Join-Path $runDir 'online-b.json'), '--character', "$($foreign.id)", '--ticks', '45')
    if ((Get-Content -LiteralPath $deniedLog -Raw -Encoding UTF8) -match 'Online spawn confirmed:') { throw 'Foreign character was admitted' }
    $serverLog = Wait-App $server.Process 'server'
    if ($serverLog -notmatch '2D admission rejected: Character ownership mismatch') { throw 'Server did not enforce ownership at app admission' }
    $state = Get-Content -LiteralPath $stateFile -Raw -Encoding UTF8 | ConvertFrom-Json
    $ca = $state.characters.characters | Where-Object { $_.name -eq 'online-a' }
    $cb = $state.characters.characters | Where-Object { $_.name -eq 'online-b' }
    foreach ($character in @($ca, $cb)) {
        if (-not $character -or $character.world3D -or $character.posZ -ne 0 -or $character.floorLevel -ne 1 -or
            $character.sceneId -ne $manifest.mainScene) { throw 'Incorrect persisted 2D scene/floor contract' }
    }
    if ([Math]::Abs($ca.posX - 1.345) -gt 0.00001 -or [Math]::Abs($cb.posX + 2.25) -gt 0.00001 -or
        $ca.posY -ne 0 -or $cb.posY -ne 0 -or [Math]::Abs($ca.facingRadians - [Math]::PI / 2) -gt 0.00001 -or
        [Math]::Abs($cb.facingRadians + [Math]::PI / 2) -gt 0.00001) { throw 'Final authority motion was not preserved' }
    $server = Start-Server 'restart' 180
    $owned += $server.Process
    $reconnect = Start-Player 'reconnect' 'online-a' $idleInput $server.Port
    $owned += $reconnect
    $reconnectLog = Wait-App $reconnect 'reconnect'
    if ($reconnectLog -notmatch 'Online spawn confirmed: .*dimension=2D, position=\(([^,]+), ([^)]+)\), floor=1' -or
        $reconnectLog -notmatch 'Input replay confirmed: steps=20, pending=0') { throw 'Reconnected at an incorrect persisted spawn' }
    if ($reconnectLog -match 'Online spawn confirmed: .*dimension=2D, position=\(([^,]+), ([^)]+)\), floor=1') {
        $spawnX = [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
        $spawnY = [double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture)
        if ([Math]::Abs($spawnX - $ca.posX) -gt 0.00001 -or $spawnY -ne $ca.posY) { throw 'Saved coordinates were not used for reconnect' }
    }
    $null = Wait-App $server.Process 'restart'
    $reloaded = Get-Content -LiteralPath $stateFile -Raw -Encoding UTF8 | ConvertFrom-Json
    $restored = $reloaded.characters.characters | Where-Object { $_.name -eq 'online-a' }
    if ($restored.posX -ne $ca.posX -or $restored.posY -ne $ca.posY -or $restored.floorLevel -ne $ca.floorLevel -or
        $restored.facingRadians -ne $ca.facingRadians) { throw 'Idle reconnect changed saved motion' }
    if ((Get-FileHash -LiteralPath $sceneFile).Hash -ne $sceneHash) { throw 'Playing changed authored scene data' }
    Write-Output "PASS: $Configuration official 2D apps, two accounts/visible sprites, shared circle/offset/floor thin-wall collision, acknowledgments, peer removal, Lua isolation, save/restart/reconnect ($runDir)"
} finally {
    foreach ($process in $owned) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        $process.Dispose()
    }
}
