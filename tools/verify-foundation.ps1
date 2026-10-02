param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$BuildDir = 'build/dev'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$serverExe = Join-Path $buildRoot "apps/server/$Configuration/MyServer.exe"
if (-not (Test-Path -LiteralPath $serverExe)) { throw "Build MyServer first: $serverExe" }
$playerExe = Join-Path $buildRoot "apps/game/$Configuration/MyGame.exe"
if (-not (Test-Path -LiteralPath $playerExe)) { throw "Build MyGame first: $playerExe" }
$runDir = Join-Path $repoRoot ('build/foundation/server-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDir | Out-Null
$script:step = 0

function Invoke-ServerCheck([int]$ExpectedExit, [string[]]$Arguments) {
    $script:step++
    & $serverExe @Arguments --data $runDir > (Join-Path $runDir "step-$script:step.log") 2>&1
    if ($LASTEXITCODE -ne $ExpectedExit) {
        throw "Step $script:step expected exit $ExpectedExit, received $LASTEXITCODE; logs: $runDir"
    }
}

Invoke-ServerCheck 64 @('--port', '70000')
Invoke-ServerCheck 64 @('--tickrate', 'invalid')
Invoke-ServerCheck 64 @('--bots', '65')
Invoke-ServerCheck 0 @('--register', 'tester', 'test-only-password')
Invoke-ServerCheck 0 @('--make-char', 'tester', 'TestCharacter')
Invoke-ServerCheck 0 @('--port', '0', '--tickrate', '60', '--bots', '1', '--ticks', '125', '--autosave', '1')
$snapshotPath = Join-Path $runDir 'state.json'
$snapshot = Get-Content -LiteralPath $snapshotPath -Raw -Encoding UTF8 | ConvertFrom-Json
$bot = $snapshot.characters.characters | Where-Object { $_.name -eq 'bot1' }
if (-not $bot -or $bot.hp -le 0 -or ([Math]::Abs($bot.posX) + [Math]::Abs($bot.posY)) -le 0.1) {
    throw 'Active bot state was not persisted'
}
if (@($snapshot.accounts.accounts | Where-Object { $_.algorithm -ne 'pbkdf2-sha256' }).Count -ne 0) {
    throw 'Unexpected password storage format'
}
Invoke-ServerCheck 0 @('--port', '0', '--ticks', '0')
$reloaded = Get-Content -LiteralPath $snapshotPath -Raw -Encoding UTF8 | ConvertFrom-Json
$reloadedBot = $reloaded.characters.characters | Where-Object { $_.name -eq 'bot1' }
if ($reloadedBot.posX -ne $bot.posX -or $reloadedBot.posY -ne $bot.posY -or $reloadedBot.hp -ne $bot.hp) {
    throw 'Restart changed persisted bot state'
}
# Relative sleeps accumulate Windows timer rounding and starve 60 Hz clients.
$tickClock = [Diagnostics.Stopwatch]::StartNew()
Invoke-ServerCheck 0 @('--port', '0', '--tickrate', '60', '--ticks', '120')
$tickClock.Stop()
if ($tickClock.Elapsed.TotalSeconds -gt 2.8) {
    throw "120 server ticks exceeded the 60 Hz deadline allowance: $($tickClock.Elapsed.TotalSeconds)s"
}
Write-Output "PASS: 120 server ticks in $($tickClock.Elapsed.TotalSeconds.ToString('F3'))s (60 Hz, 0.8s startup/scheduling allowance)"
[IO.File]::WriteAllText($snapshotPath, '{}', [Text.UTF8Encoding]::new($false))
$before = (Get-FileHash -LiteralPath $snapshotPath).Hash
Invoke-ServerCheck 4 @('--port', '0', '--ticks', '0')
if ((Get-FileHash -LiteralPath $snapshotPath).Hash -ne $before) { throw 'Failed load overwrote data' }
Write-Output "PASS: CLI bounds, registration, active-session autosave/shutdown, restart, corrupt-save refusal ($runDir)"

# Native project loading is checked separately from server state and shared runtime unit tests.
$playerDir = Join-Path $repoRoot ('build/foundation/player-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $playerDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'game/starter/meadow_village') -Destination (Join-Path $playerDir 'project') -Recurse
$projectFile = Join-Path $playerDir 'project/project.myeproj'
$script:playerStep = 0
function Invoke-PlayerCheck([int]$ExpectedExit, [string[]]$Arguments) {
    $script:playerStep++
    # Windows PowerShell turns expected native stderr into an ErrorRecord before exit-code checks.
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $playerExe @Arguments > (Join-Path $playerDir "step-$script:playerStep.log") 2>&1
        $actualExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($actualExit -ne $ExpectedExit) {
        throw "Player step $script:playerStep expected exit $ExpectedExit, received $actualExit; logs: $playerDir"
    }
}
Invoke-PlayerCheck 64 @('--headless')
Invoke-PlayerCheck 64 @('--make-sample')
Invoke-PlayerCheck 64 @('--project', $projectFile, '--frames', 'invalid')
$frame = Join-Path $playerDir 'frame.bmp'
Invoke-PlayerCheck 0 @('--project', $projectFile, '--headless', '--frames', '30', '--dump', $frame)
if (-not (Test-Path -LiteralPath $frame) -or (Get-Item -LiteralPath $frame).Length -le 54) { throw 'Project player capture was not produced' }
$bitmapHeader = [IO.File]::ReadAllBytes($frame)
if ($bitmapHeader[0] -ne 0x42 -or $bitmapHeader[1] -ne 0x4D -or
    [BitConverter]::ToInt32($bitmapHeader, 18) -ne 960 -or [Math]::Abs([BitConverter]::ToInt32($bitmapHeader, 22)) -ne 540) {
    throw 'Project player did not render the contracted 960x540 BMP target'
}
Invoke-PlayerCheck 1 @('--project', $projectFile, '--headless', '--frames', '1', '--scene', '../outside.scene')
$projectBefore = Get-Content -LiteralPath $projectFile -Raw -Encoding UTF8
$manifest = $projectBefore | ConvertFrom-Json
$sceneFile = Join-Path (Split-Path -Parent $projectFile) $manifest.mainScene

# Drive the authored event/map/Lua path without relying on synthetic OS keyboard input.
$village = Get-Content -LiteralPath $sceneFile -Raw -Encoding UTF8 | ConvertFrom-Json
$door = $village.entities | Where-Object { $_.components.ObjectName.value -eq 'Cottage Door' }
if (-not $door) { throw 'Starter portal fixture missing' }
$doorBehavior = $door.components.ObjectBehavior
if (-not $doorBehavior) {
    $doorBehavior = [pscustomobject]@{ connections = @(); luaSource = '' }
    $door.components | Add-Member -MemberType NoteProperty -Name ObjectBehavior -Value $doorBehavior
}
$doorBehavior.connections += [pscustomobject]@{
    event = 'Start'; action = 'ChangeMap'; target = 'Cottage Spawn'; text = 'assets/scenes/cottage.scene'; x = 0; y = 0; visible = $true
}
[IO.File]::WriteAllText($sceneFile, ($village | ConvertTo-Json -Depth 24), [Text.UTF8Encoding]::new($false))
$cottageFile = Join-Path (Split-Path -Parent $projectFile) 'assets/scenes/cottage.scene'
$cottage = Get-Content -LiteralPath $cottageFile -Raw -Encoding UTF8 | ConvertFrom-Json
$spawn = $cottage.entities | Where-Object { $_.components.ObjectName.value -eq 'Cottage Spawn' }
$character = $cottage.entities | Where-Object { $_.components.CharacterController2D.enabled }
if (-not $spawn -or -not $character) { throw 'Starter destination fixture missing' }
$spawn.components.LocalTransform.px = 2
$spawn.components.LocalTransform.py = -1
$spawnX = $spawn.components.LocalTransform.px.ToString([Globalization.CultureInfo]::InvariantCulture)
$spawnY = $spawn.components.LocalTransform.py.ToString([Globalization.CultureInfo]::InvariantCulture)
$behavior = [pscustomobject]@{ connections = @(); luaSource = @"
return { on_init = function(self)
  local p = mye.world.entity_from_packed(self.entity):get_position()
  assert(math.abs(p.x - $spawnX) < 0.001 and math.abs(p.y - $spawnY) < 0.001, 'destination spawn mismatch')
  mye.log('player-map-spawn-pass')
end }
"@ }
$character.components | Add-Member -MemberType NoteProperty -Name ObjectBehavior -Value $behavior -Force
[IO.File]::WriteAllText($cottageFile, ($cottage | ConvertTo-Json -Depth 24), [Text.UTF8Encoding]::new($false))
Invoke-PlayerCheck 0 @('--project', $projectFile, '--headless', '--frames', '120')
$mapLog = Get-Content -LiteralPath (Join-Path $playerDir "step-$script:playerStep.log") -Raw -Encoding UTF8
if ($mapLog -notmatch 'Scene loaded: assets/scenes/cottage.scene' -or $mapLog -notmatch 'player-map-spawn-pass') {
    throw 'Authored map request, destination spawn and Lua initialization did not complete'
}
$sceneHash = (Get-FileHash -LiteralPath $sceneFile).Hash
$manifest.version = 99
[IO.File]::WriteAllText($projectFile, ($manifest | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Invoke-PlayerCheck 1 @('--project', $projectFile, '--headless', '--frames', '1')
if ((Get-FileHash -LiteralPath $sceneFile).Hash -ne $sceneHash) { throw 'Failed project load modified the scene' }
Write-Output "PASS: explicit project CLI, removed demo options, GUID-based native capture, scene bounds, event/map/spawn/Lua integration, invalid-version refusal ($playerDir)"
exit 0
