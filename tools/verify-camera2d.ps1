param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$BuildDir = 'build/dev'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$gameExe = Join-Path $buildRoot "apps/game/$Configuration/MyGame.exe"
$editorExe = Join-Path $buildRoot "apps/editor/$Configuration/MyEditor.exe"
foreach ($exe in @($gameExe, $editorExe)) {
    if (-not (Test-Path -LiteralPath $exe)) { throw "Build the apps first: $exe" }
}
$runDir = Join-Path $repoRoot ('build/camera2d/' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'game/starter/meadow_village') -Destination (Join-Path $runDir 'project') -Recurse
$project = Join-Path $runDir 'project/project.myeproj'
$root = Split-Path -Parent $project
$manifest = Get-Content -LiteralPath $project -Raw -Encoding UTF8 | ConvertFrom-Json
$sceneFile = Join-Path $root $manifest.mainScene
$authored = Get-Content -LiteralPath $sceneFile -Raw -Encoding UTF8 | ConvertFrom-Json
$player = $authored.entities | Where-Object { $_.components.CharacterController2D.enabled }
if (@($player).Count -ne 1) { throw 'Starter needs one controller' }
$player.components.LocalTransform.px = -8; $player.components.LocalTransform.py = 0
$player.components.SpriteRenderer.visible = $false
$player.components.CharacterController2D.speed = 0
$texturePath = Join-Path $root 'assets/environment/terrain_tiles.png'
$meta = Get-Content -LiteralPath ($texturePath + '.meta') -Raw -Encoding UTF8 | ConvertFrom-Json
Add-Type -AssemblyName System.Drawing
$texture = [Drawing.Bitmap]::new($texturePath)
try { $textureWidth = $texture.Width; $textureHeight = $texture.Height } finally { $texture.Dispose() }
function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
}
function Pose([double]$X, [double]$Y) {
    return [pscustomobject]@{ __version = 1; px = $X; py = $Y; pz = 0; rx = 0; ry = 0; rz = 0; rw = 1; sx = 1; sy = 1; sz = 1 }
}
function Marker([int]$Id, [double]$X, [double]$Y, [int]$Channel) {
    return [pscustomobject]@{ id = $Id; components = [pscustomobject]@{
        LocalTransform = (Pose $X $Y)
        SpriteRenderer = [pscustomobject]@{ __version = 1; sprite = [pscustomobject]@{ guid = $meta.guid; type = '0' }
            uvx = 0; uvy = 0; uvw = 16.0 / $textureWidth; uvh = 16.0 / $textureHeight; pvx = 8; pvy = 8
            tr = [int]($Channel -eq 0); tg = [int]($Channel -eq 1); tb = [int]($Channel -eq 2); ta = 1
            visible = $true; flipX = $false; flipY = $false; sortLayer = 100; orderInLayer = 0 }
    } }
}
$camera = [pscustomobject]@{ id = 904; components = [pscustomobject]@{
    ObjectName = [pscustomobject]@{ value = 'Camera' }; LocalTransform = (Pose 50 50)
    Camera2D = [pscustomobject]@{ __version = 1; current = $true; followTarget = 'Anchor'
        offset = [pscustomobject]@{ x = 0.003; y = 0.002 }; deadzoneHalf = [pscustomobject]@{ x = 0; y = 0 }
        zoom = 1; pixelSnap = $true; boundsEnabled = $false
        bounds = [pscustomobject]@{ x = 0; y = 0; w = 4; h = 4 } }
} }
$authored.entities = @($player, (Marker 900 0 0 0), (Marker 901 2 0 1), (Marker 902 0 2 2),
    [pscustomobject]@{ id = 903; components = [pscustomobject]@{
        ObjectName = [pscustomobject]@{ value = 'Anchor' }; LocalTransform = (Pose .25 .125) } }, $camera)
$script:step = 0
function Invoke-App([string]$Exe, [int]$Expected, [string[]]$Arguments) {
    $script:step++
    $log = Join-Path $runDir "step-$script:step.log"
    $previous = $ErrorActionPreference
    try { $ErrorActionPreference = 'Continue'; & $Exe @Arguments > $log 2>&1; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $previous }
    if ($code -ne $Expected) { throw "Expected exit $Expected, received $code; $log" }
    return Get-Content -LiteralPath $log -Raw -Encoding UTF8
}
# This reads actual DX11 frames; the colored markers use an existing opaque tile/GUID.
Add-Type -TypeDefinition @'
using System.Drawing;
public static class CameraFrameBounds {
    public static int[] Read(string file, int channel) {
        using (var image = new Bitmap(file)) {
            if (image.Width != 960 || image.Height != 540) throw new System.Exception("Incorrect pixel target");
            int minX=960, maxX=-1, minY=540, maxY=-1;
            for (int y=0; y<540; y++) for (int x=0; x<960; x++) {
                var c=image.GetPixel(x,y);
                if ((channel==0 && c.R>0 && c.G==0 && c.B==0) ||
                    (channel==1 && c.G>0 && c.R==0 && c.B==0) ||
                    (channel==2 && c.B>0 && c.R==0 && c.G==0)) {
                    minX=System.Math.Min(minX,x); maxX=System.Math.Max(maxX,x);
                    minY=System.Math.Min(minY,y); maxY=System.Math.Max(maxY,y);
                }
            }
            return new int[] {minX,maxX,minY,maxY};
        }
    }
}
'@ -ReferencedAssemblies System.Drawing
function Check-Bounds([string]$File, [int]$Channel, [int[]]$Expected) {
    $actual = [CameraFrameBounds]::Read($File, $Channel)
    if (($actual -join ',') -ne ($Expected -join ',')) { throw "Camera pixel bounds $actual != $Expected : $File" }
}
foreach ($zoom in @(1, 2)) {
    $camera.components.Camera2D.zoom = $zoom
    Write-Json $sceneFile $authored
    $hash = (Get-FileHash -LiteralPath $sceneFile).Hash
    $gameFrame = Join-Path $runDir "game-zoom-$zoom.bmp"
    $editorFrame = Join-Path $runDir "editor-zoom-$zoom.bmp"
    $null = Invoke-App $gameExe 0 @('--project', $project, '--headless', '--ticks', '4', '--dump', $gameFrame)
    $null = Invoke-App $editorExe 0 @('--project', $project, '--play', '--headless', '--frames', '40', '--dump', $editorFrame)
    if ((Get-FileHash -LiteralPath $gameFrame).Hash -ne (Get-FileHash -LiteralPath $editorFrame).Hash) { throw 'Saved camera differs between actual Play/MyGame frames' }
    if ((Get-FileHash -LiteralPath $sceneFile).Hash -ne $hash) { throw 'Play modified authored camera settings' }
    if ($zoom -eq 1) {
        Check-Bounds $gameFrame 0 @(460,475,268,283)
        Check-Bounds $gameFrame 1 @(556,571,268,283)
        Check-Bounds $gameFrame 2 @(460,475,172,187)
    } else {
        Check-Bounds $gameFrame 0 @(440,471,266,297)
        Check-Bounds $gameFrame 1 @(632,663,266,297)
        Check-Bounds $gameFrame 2 @(440,471,74,105)
    }
}
$camera.components.Camera2D.zoom = 1
$camera.components.Camera2D.boundsEnabled = $true
Write-Json $sceneFile $authored
$smallFrame = Join-Path $runDir 'small-bounds.bmp'
$null = Invoke-App $gameExe 0 @('--project', $project, '--headless', '--ticks', '4', '--dump', $smallFrame)
Check-Bounds $smallFrame 0 @(376,391,358,373)
$camera.components.Camera2D.boundsEnabled = $false
Write-Json $sceneFile $authored
$inputFile = Join-Path $runDir 'wheel.json'
Write-Json $inputFile ([pscustomobject]@{ version = 1; steps = @(@{ticks=1; x=0; y=0; cameraZoomSteps=1}, @{ticks=3; x=0; y=0}) })
$wheelFrame = Join-Path $runDir 'wheel.bmp'
$null = Invoke-App $gameExe 0 @('--project', $project, '--headless', '--input', $inputFile, '--dump', $wheelFrame)
Check-Bounds $wheelFrame 0 @(458,475,268,285)
foreach ($invalid in @('bad', 17)) {
    Write-Json $inputFile ([pscustomobject]@{ version = 1; steps = @(@{ticks=1; x=0; y=0; cameraZoomSteps=$invalid}) })
    $null = Invoke-App $gameExe 1 @('--project', $project, '--headless', '--input', $inputFile)
}
$camera.components | Add-Member -NotePropertyName ObjectBehavior -NotePropertyValue ([pscustomobject]@{
    connections = @(); luaSource = @'
return {on_init=function(self)
    local camera=mye.world.entity_from_packed(self.entity)
    camera:set_camera_zoom(2)
    local p=camera:world_to_screen(mye.Vec2(0,0))
    assert(math.abs(p.x-455.712)<.001 and math.abs(p.y-282.192)<.001)
    assert(camera:screen_to_world(p):length()<.0001)
    camera:shake_camera(.1,.5)
    mye.log('camera2d-logical-coordinates-verified')
end}
'@
})
Write-Json $sceneFile $authored
$shakeFrame = Join-Path $runDir 'shake.bmp'
$log = Invoke-App $gameExe 0 @('--project', $project, '--headless', '--ticks', '4', '--dump', $shakeFrame)
if ($log -notmatch 'camera2d-logical-coordinates-verified' -or $log -match '\[ERROR\]') { throw 'Lua camera API check failed' }
Check-Bounds $shakeFrame 0 @(447,478,258,289)
$camera.components.PSObject.Properties.Remove('ObjectBehavior')
$camera.components.Camera2D.zoom = 0
Write-Json $sceneFile $authored
$null = Invoke-App $gameExe 1 @('--project', $project, '--headless', '--ticks', '1')
$null = Invoke-App $editorExe 1 @('--project', $project, '--play', '--headless', '--frames', '1')
Write-Output "Camera2D verification passed ($Configuration): $runDir"
