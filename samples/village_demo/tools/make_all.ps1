# make_all.ps1 — regenerate ALL village_demo binary assets deterministically.
#
# Runs the three generators in order. Data files (maps/dialogue/loc JSON, Lua scripts) are authored
# by hand and are NOT regenerated here. Idempotent: re-running overwrites byte-for-byte identical
# output (fixed seeds / closed-form synthesis).
#
# Usage: powershell -ExecutionPolicy Bypass -File make_all.ps1
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot

Write-Host "== tiles =="
& powershell -ExecutionPolicy Bypass -File (Join-Path $here "make_tiles.ps1")
Write-Host "== chars =="
& powershell -ExecutionPolicy Bypass -File (Join-Path $here "make_chars.ps1")
Write-Host "== audio =="
& powershell -ExecutionPolicy Bypass -File (Join-Path $here "make_audio.ps1")
Write-Host "== props (python) =="
& python (Join-Path $here "make_props.py")

Write-Host "== ALL village_demo assets regenerated =="
