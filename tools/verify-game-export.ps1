param([Parameter(Mandatory=$true)][string]$Runtime)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$distribution = (Resolve-Path -LiteralPath $Runtime).Path
$run = Join-Path $repo ('build/game-export/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'game/starter/meadow_village') -Destination (Join-Path $run 'source') -Recurse
$project = Join-Path $run 'source/project.myeproj'
$output = Join-Path $run 'game'
New-Item -ItemType Directory -Path (Join-Path $run 'launch') | Out-Null
function Invoke-Check([string]$Exe, [string]$Name, [int]$Expected, [string[]]$Arguments) {
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'; & $Exe @Arguments > (Join-Path $run "$Name.log") 2>&1; $actual=$LASTEXITCODE
    } finally { $ErrorActionPreference=$previous }
    if ($actual -ne $Expected) { throw "$Name expected $Expected, got $actual ($run)" }
}
Invoke-Check (Join-Path $distribution 'MyGame.exe') 'source-play' 0 @('--project',$project,'--headless','--ticks','4','--dump',(Join-Path $run 'source.bmp'))
Invoke-Check (Join-Path $distribution 'MyEditor.exe') 'export' 0 @('--project',$project,'--export-game',$output,'--runtime',$distribution)
$before = (Get-FileHash (Join-Path $output 'MyGame.exe')).Hash
Invoke-Check (Join-Path $distribution 'MyEditor.exe') 'occupied' 1 @('--project',$project,'--export-game',$output,'--runtime',$distribution)
if ((Get-FileHash (Join-Path $output 'MyGame.exe')).Hash -ne $before) { throw 'Occupied export changed the player' }
foreach ($excluded in @('MyEditor.exe','MyServer.exe','docs','state.json','.agents')) {
    if (Test-Path -LiteralPath (Join-Path $output $excluded)) { throw "Private/producer file exported: $excluded" }
}
if (-not (Test-Path -LiteralPath (Join-Path $output 'licenses/NOTICE.txt')) -or
    -not (Test-Path -LiteralPath (Join-Path $output 'Play.cmd'))) { throw 'Launcher or notices missing' }
$source = [IO.Path]::GetFullPath((Join-Path $run 'source'))
$scope = [IO.Path]::GetFullPath($run) + [IO.Path]::DirectorySeparatorChar
if (-not $source.StartsWith($scope,[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture source escaped run directory' }
Rename-Item -LiteralPath $source -NewName 'source-hidden'
Push-Location (Join-Path $run 'launch')
try {
    Invoke-Check (Join-Path $output 'MyGame.exe') 'independent-play' 0 @('--project',(Join-Path $output 'game.myeproj'),
        '--headless','--ticks','4','--dump',(Join-Path $run 'exported.bmp'))
} finally { Pop-Location }
if ((Get-FileHash (Join-Path $run 'source.bmp')).Hash -ne (Get-FileHash (Join-Path $run 'exported.bmp')).Hash) {
    throw 'Exported render differs from the source'
}
New-Item -ItemType Directory -Path (Join-Path $run 'linked-source') | Out-Null
Copy-Item -LiteralPath (Join-Path $run 'source-hidden/project.myeproj') -Destination (Join-Path $run 'linked-source/project.myeproj')
New-Item -ItemType Junction -Path (Join-Path $run 'linked-source/assets') -Target (Join-Path $run 'source-hidden/assets') | Out-Null
Invoke-Check (Join-Path $distribution 'MyEditor.exe') 'linked-root' 1 @('--project',(Join-Path $run 'linked-source/project.myeproj'),
    '--export-game',(Join-Path $run 'refused-link'),'--runtime',$distribution)
if (Test-Path -LiteralPath (Join-Path $run 'refused-link')) { throw 'Linked asset root was published' }
Write-Host "Game export checks passed (independent pixels, occupied output, linked asset root): $run"
exit 0
