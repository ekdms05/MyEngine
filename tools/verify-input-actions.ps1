param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$BuildDir = 'build/dev'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repo $BuildDir }
$game = Join-Path $build "apps/game/$Configuration/MyGame.exe"
$editor = Join-Path $build "apps/editor/$Configuration/MyEditor.exe"
foreach ($exe in @($game, $editor)) { if (-not (Test-Path -LiteralPath $exe)) { throw "Build first: $exe" } }
$run = Join-Path $repo ('build/input-actions/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'game/starter/meadow_village') -Destination (Join-Path $run 'project') -Recurse
$project = Join-Path $run 'project/project.myeproj'
$manifest = Get-Content -LiteralPath $project -Raw -Encoding UTF8 | ConvertFrom-Json
$scene = Join-Path (Split-Path -Parent $project) $manifest.mainScene
$sceneHash = (Get-FileHash -LiteralPath $scene).Hash
function Save-Manifest {
    [IO.File]::WriteAllText($project, ($manifest | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
}
function Run-App([string]$Exe, [string]$Name, [string[]]$Arguments, [int]$Expected = 0) {
    $stdout = Join-Path $run ($Name + '.out.log'); $stderr = Join-Path $run ($Name + '.err.log')
    $process = Start-Process -FilePath $Exe -ArgumentList ($Arguments | ForEach-Object { '"' + $_ + '"' }) `
        -WorkingDirectory $repo -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(30000)) { throw "$Name timed out" }
        if ($process.ExitCode -ne $Expected) {
            throw "$Name exit=$($process.ExitCode), expected=$Expected`n$(Get-Content -LiteralPath $stdout -Raw -Encoding UTF8)`n$(Get-Content -LiteralPath $stderr -Raw -Encoding UTF8)"
        }
    } finally { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }; $process.Dispose() }
}
function Require-Capture([string]$Name) {
    $path = Join-Path $run ($Name + '.bmp')
    if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -lt 1024) { throw "$Name capture missing" }
}
$inputMap = [pscustomobject]@{ version = 1; actions = @(
    [pscustomobject]@{ name = 'move_right'; deadzone = .2; bindings = @([pscustomobject]@{ device = 'key'; code = 15; direction = 1; pad = 0 }) },
    [pscustomobject]@{ name = 'move_up'; deadzone = .2; bindings = @([pscustomobject]@{ device = 'key'; code = 12; direction = 1; pad = 0 }) },
    [pscustomobject]@{ name = 'interact'; deadzone = .2; bindings = @([pscustomobject]@{ device = 'mouse'; code = 3; direction = 1; pad = 0 }) }
) }
$manifest | Add-Member -MemberType NoteProperty -Name inputMap -Value $inputMap -Force
Save-Manifest
Run-App $game 'authored-game' @('--project', $project, '--headless', '--ticks', '4', '--dump', (Join-Path $run 'authored-game.bmp'))
Run-App $editor 'authored-play' @('--project', $project, '--headless', '--play', '--frames', '8', '--dump', (Join-Path $run 'authored-play.bmp'))
Require-Capture 'authored-game'; Require-Capture 'authored-play'
Run-App $editor 'settings-ui' @('--project', $project, '--input-settings-dialog', '--frames', '12', '--dump', (Join-Path $run 'settings-ui.bmp'))
Require-Capture 'settings-ui'
Run-App $editor 'settings-while-playing' @('--project', $project, '--input-settings-dialog', '--play', '--headless', '--frames', '3') 1
$manifest.inputMap.version = 2; Save-Manifest
Run-App $game 'bad-map-game' @('--project', $project, '--headless', '--ticks', '4') 1
Run-App $editor 'bad-map-editor' @('--project', $project, '--headless', '--frames', '3') 1
$manifest.inputMap.version = 1
$manifest.inputMap.actions[0].bindings[0].code = 999; Save-Manifest
Run-App $game 'bad-code-game' @('--project', $project, '--headless', '--ticks', '4') 1
Run-App $editor 'bad-code-editor' @('--project', $project, '--headless', '--frames', '3') 1
$manifest.PSObject.Properties.Remove('inputMap'); Save-Manifest
Run-App $game 'legacy-game' @('--project', $project, '--headless', '--ticks', '4')
if ((Get-FileHash -LiteralPath $scene).Hash -ne $sceneHash) { throw 'Input checks modified the scene' }
Write-Output "Input actions app checks passed ($Configuration): $run"
Write-Output 'CLI rendering/validation and synthetic library tests do not certify physical keyboard/gamepad operation.'
