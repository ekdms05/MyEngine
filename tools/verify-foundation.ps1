param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$serverExe = Join-Path $repoRoot "build/dev/apps/server/$Configuration/MyServer.exe"
if (-not (Test-Path -LiteralPath $serverExe)) { throw "Build MyServer first: $serverExe" }
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
[IO.File]::WriteAllText($snapshotPath, '{}', [Text.UTF8Encoding]::new($false))
$before = (Get-FileHash -LiteralPath $snapshotPath).Hash
Invoke-ServerCheck 4 @('--port', '0', '--ticks', '0')
if ((Get-FileHash -LiteralPath $snapshotPath).Hash -ne $before) { throw 'Failed load overwrote data' }
Write-Output "PASS: CLI bounds, registration, active-session autosave/shutdown, restart, corrupt-save refusal ($runDir)"
