# Packages reviewed Release artifacts only; never deletes an existing stage.
param(
    [ValidateSet('Release')][string]$Config = 'Release',
    [string]$BuildDir = 'build/dev',
    [string]$OutDir = 'build/packages',
    [string]$Version = '',
    [switch]$NoZip,
    [switch]$AllowDirtyForReview
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$productVersion = (Get-Content -LiteralPath (Join-Path $repo 'VERSION') -Raw).Trim()
if (-not $Version) { $Version = $productVersion }
if ($Version -ne $productVersion -or $Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must match VERSION (major.minor.patch)' }
$mcpVersion = (Get-Content -LiteralPath (Join-Path $repo 'tools/mcp/package.json') -Raw | ConvertFrom-Json).version
if ($mcpVersion -ne $Version) { throw 'MCP version must match VERSION' }
$sourceModified = [bool](& git -C $repo status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw 'Cannot read source status' }
if ($sourceModified -and -not $AllowDirtyForReview) { throw 'Commit the reviewed source before packaging, or use -AllowDirtyForReview for a local candidate.' }
$revision = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot read source revision' }

function Repo-File([string]$relative) {
    $file = Join-Path $repo $relative
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required package input missing: $relative" }
    return $file
}
function Write-Utf8([string]$file, [string]$text) {
    [IO.File]::WriteAllText($file, $text, [Text.UTF8Encoding]::new($false))
}

$editor = Repo-File "$BuildDir/apps/editor/$Config/MyEditor.exe"
$player = Repo-File "$BuildDir/apps/game/$Config/MyGame.exe"
$server = Repo-File "$BuildDir/apps/server/$Config/MyServer.exe"
$uiFont = Repo-File 'assets/fonts/NanumSquareRoundR.ttf'
$uiFontLicense = Repo-File 'assets/fonts/NanumSquareRound-LICENSE.txt'
$outRoot = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $repo $OutDir }))
$stageName = "MyEngine-$Version-windows-x64"
$stage = Join-Path $outRoot $stageName
$zip = "$stage.zip"
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $zip) -or (Test-Path -LiteralPath "$zip.sha256")) {
    throw 'Package output already exists. Choose a fresh output directory; existing files are preserved.'
}

$publicDocs = @('README.md','01-core-platform.md','02-rendering.md','03-scene-world.md','04-asset-pipeline.md',
    '06-runtime-systems.md','07-editor-ui.md','08-mcp.md','13-architecture-and-features.md',
    '14-development-priorities.md','15-skills-and-agents.md','16-foundation-worklog.md',
    '17-object-workflow.md','19-lua-api.md','20-components.md','21-3d-play-and-online.md','22-2d-mmorpg-roadmap.md','23-2d-online-play.md','24-2d-camera.md','25-input-actions.md','26-2d-animation.md','27-game-ui.md','release-notes.md')
foreach ($name in $publicDocs) { $null = Repo-File "docs/$name" }
$null = Repo-File 'docs/guide/index.html'
$null = Repo-File 'game/starter/meadow_village/project.myeproj'

New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath $editor -Destination (Join-Path $stage 'MyEditor.exe')
Copy-Item -LiteralPath $player -Destination (Join-Path $stage 'MyGame.exe')
Copy-Item -LiteralPath $server -Destination (Join-Path $stage 'MyServer.exe')
New-Item -ItemType Directory -Path (Join-Path $stage 'fonts') | Out-Null
Copy-Item -LiteralPath $uiFont -Destination (Join-Path $stage 'fonts/NanumSquareRoundR.ttf')
New-Item -ItemType Directory -Path (Join-Path $stage 'templates') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'game/starter/meadow_village') -Destination (Join-Path $stage 'templates/meadow_village') -Recurse
$templateReadme = Join-Path $stage 'templates/meadow_village/README.md'
Write-Utf8 $templateReadme ([IO.File]::ReadAllText($templateReadme).Replace('../../../docs/', '../../docs/'))
New-Item -ItemType Directory -Path (Join-Path $stage 'docs') | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'docs/guide') -Destination (Join-Path $stage 'docs/guide') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'docs/images') -Destination (Join-Path $stage 'docs/images') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'docs/examples') -Destination (Join-Path $stage 'docs/examples') -Recurse
foreach ($name in $publicDocs) { Copy-Item -LiteralPath (Repo-File "docs/$name") -Destination (Join-Path $stage "docs/$name") }
Copy-Item -LiteralPath (Repo-File 'LICENSE') -Destination (Join-Path $stage 'LICENSE')
Copy-Item -LiteralPath (Repo-File 'README.md') -Destination (Join-Path $stage 'README.md')

# Source references remain useful without adding source/build trees to the binary ZIP.
$documentationFiles = @((Join-Path $stage 'README.md')) + @($publicDocs | ForEach-Object { Join-Path $stage "docs/$_" }) +
    @(Get-ChildItem -LiteralPath (Join-Path $stage 'docs/guide') -Filter '*.html' -File -Recurse | ForEach-Object FullName)
foreach ($file in $documentationFiles) {
    $text = [IO.File]::ReadAllText($file)
    $sourceLinks = '(?<open>\]\(|(?:href|src)=["''])(?:\.\./)*(?<path>(?:engine|apps|tools|tests|\.github)/[^)"'']+)(?<close>\)|["''])'
    $converted = [Text.RegularExpressions.Regex]::Replace($text, $sourceLinks, [Text.RegularExpressions.MatchEvaluator]{
        param($match)
        $sourcePath = $match.Groups['path'].Value
        $kind = if (Test-Path -LiteralPath (Join-Path $repo $sourcePath) -PathType Container) { 'tree' } else { 'blob' }
        return $match.Groups['open'].Value + "https://github.com/ekdms05/MyEngine/$kind/$revision/$sourcePath" + $match.Groups['close'].Value
    })
    Write-Utf8 $file $converted
}

# License notices are read from the linked vendored sources, not vendor build trees.
$licenseDir = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Path $licenseDir | Out-Null
Copy-Item -LiteralPath $uiFontLicense -Destination (Join-Path $licenseDir 'NanumSquareRound-LICENSE.txt')
Copy-Item -LiteralPath (Repo-File 'third_party/imgui/LICENSE.txt') -Destination (Join-Path $licenseDir 'imgui.txt')
Copy-Item -LiteralPath (Repo-File 'third_party/godot/LICENSE.txt') -Destination (Join-Path $licenseDir 'godot.txt')
Copy-Item -LiteralPath (Repo-File 'third_party/godot/AUTHORS.md') -Destination (Join-Path $licenseDir 'godot-authors.md')
Copy-Item -LiteralPath (Repo-File 'tools/package/licenses/imgui-fonts.txt') -Destination (Join-Path $licenseDir 'imgui-fonts.txt')
Copy-Item -LiteralPath (Repo-File 'third_party/freetype/LICENSE.TXT') -Destination (Join-Path $licenseDir 'freetype-license-selection.txt')
Copy-Item -LiteralPath (Repo-File 'tools/package/licenses/FTL.TXT') -Destination (Join-Path $licenseDir 'FTL.TXT')
function Copy-LicenseBlock([string]$source, [string]$marker, [string]$name) {
    $text = [IO.File]::ReadAllText((Repo-File $source))
    $start = $text.LastIndexOf($marker, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "License marker missing: $source" }
    Write-Utf8 (Join-Path $licenseDir $name) $text.Substring($start)
}
Copy-LicenseBlock 'third_party/lua/lua.h' 'Copyright (C) 1994-2024 Lua.org, PUC-Rio.' 'lua.txt'
Copy-LicenseBlock 'third_party/cgltf/cgltf.h' 'cgltf is distributed under MIT license:' 'cgltf.txt'
Copy-LicenseBlock 'third_party/stb/stb_image.h' 'This software is available under 2 licenses' 'stb-image.txt'
Copy-LicenseBlock 'third_party/stb_vorbis/stb_vorbis.c' 'This software is available under 2 licenses' 'stb-vorbis.txt'
foreach ($name in @('imstb_truetype.h','imstb_rectpack.h','imstb_textedit.h')) {
    Copy-LicenseBlock "third_party/imgui/$name" 'This software is available under 2 licenses' ($name + '.txt')
}
Copy-LicenseBlock 'third_party/miniaudio/miniaudio.h' 'This software is available as a choice of the following licenses.' 'miniaudio.txt'
$zlibNotice = [IO.File]::ReadAllText((Repo-File 'third_party/freetype/src/gzip/zlib.h'))
$zlibEnd = $zlibNotice.IndexOf('*/', [StringComparison]::Ordinal)
if ($zlibEnd -lt 0) { throw 'FreeType gzip license notice missing' }
Write-Utf8 (Join-Path $licenseDir 'freetype-zlib.txt') $zlibNotice.Substring(0, $zlibEnd + 2)
foreach ($name in @('src/base/fthash.c','src/autofit/ft-hb.c')) {
    $text = [IO.File]::ReadAllText((Repo-File "third_party/freetype/$name"))
    $end = $text.IndexOf('#include', [StringComparison]::Ordinal)
    if ($end -lt 0) { throw "FreeType notice boundary missing: $name" }
    Write-Utf8 (Join-Path $licenseDir ((Split-Path $name -Leaf) + '.txt')) $text.Substring(0, $end)
}
Write-Utf8 (Join-Path $licenseDir 'NOTICE.txt') @'
This software is based in part on the work of the FreeType Team.
Portions are copyright (C) 1996-2024 The FreeType Project (https://www.freetype.org).
ImGui (including embedded stb), Lua, cgltf, stb and miniaudio notices are included beside this file.
Godot's MIT notice and authors cover the adapted vector slide projection; no Godot runtime is bundled.
ImGui's embedded ProggyClean and ProggyForever font notices are retained in imgui-fonts.txt.
FreeType's embedded zlib, X11 and Old MIT notices are retained with its selected FreeType License.
Windows system fonts are loaded from the user's OS installation and are not redistributed.
NanumSquareRound Regular is bundled under SIL OFL 1.1; its copyright and full license are included.
Starter content source/provenance is retained with templates/meadow_village.
'@

Write-Utf8 (Join-Path $stage 'README.txt') @"
MyEngine $Version - Windows x64

Run MyEditor.exe, then create or open a project in the project launcher.
The meadow village template and the offline guide (docs/guide/index.html) are included.
Play opens the project in a separate game window; pause/stop remain in the editor toolbar.
To play an existing project directly: MyGame.exe --project "path/to/project.myeproj".
Saved 2D cameras, project input bindings, eight-direction animation and action-state documents are included.
Game UI documents and local Lua HUD/input work in Play/MyGame. See docs/26-2d-animation.md and docs/27-game-ui.md.
MyServer.exe supports authenticated loopback 2D/XYZ movement. Online gameplay rules and HUD are not connected.
See docs/23-2d-online-play.md and docs/21-3d-play-and-online.md for scene contracts, private credentials and online limits.

Requirements: Windows 10/11 x64, DirectX 11 device/driver, latest Microsoft Visual C++ v14 x64 Redistributable.
Official runtime download: https://aka.ms/vc14/vc_redist.x64.exe
Editor Latin/Korean text uses the bundled NanumSquareRound Regular font.
Japanese/Chinese coverage uses installed Windows language fonts. Install the language fonts if missing.
This package contains the editor, local project player and starter, not a finished MMORPG or a game export installer.
License notices: LICENSE and licenses/. File integrity: release-manifest.json and the ZIP SHA256 sidecar.
"@

$files = @(Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName | ForEach-Object {
    @{ path = $_.FullName.Substring($stage.Length + 1).Replace('\','/'); bytes = $_.Length;
       sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$manifest = @{ version = $Version; platform = 'windows-x64'; configuration = $Config; sourceRevision = $revision;
    sourceModified = $sourceModified; files = $files }
Write-Utf8 (Join-Path $stage 'release-manifest.json') ($manifest | ConvertTo-Json -Depth 5)
if (-not $NoZip) {
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Utf8 "$zip.sha256" "$hash  $([IO.Path]::GetFileName($zip))`n"
    Write-Host "Package: $zip"
}
Write-Host "Stage: $stage ($($files.Count) files)"
