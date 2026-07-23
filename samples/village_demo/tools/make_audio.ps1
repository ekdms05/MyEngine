# make_audio.ps1 — deterministic synthesized audio for the village demo (WAV, PCM S16 44100 Hz).
#
# Produces:
#   village_bgm.wav  — ~8s seamless-looping mono pastoral melody (simple square/triangle synth over
#                      a I-V-vi-IV chord bed). Crossfade-friendly (Music().Play). Deterministic.
#   footstep.wav     — short percussive click (decaying noise + low thud). Footstep AnimEvent cue.
#   dialogue_blip.wav— tiny UI "text advance" blip (short sine ping). Dialogue box cue.
#
# All synthesis is closed-form (no RNG except a fixed-seed noise for the footstep) → byte-reproducible.
# Usage: powershell -ExecutionPolicy Bypass -File make_audio.ps1
param(
    [string]$OutDir = (Join-Path $PSScriptRoot "..\assets\audio")
)

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }
$SR = 44100

function Write-Wav {
    param([string]$Path, [double[]]$Samples)
    $n = $Samples.Length
    $dataBytes = $n * 2
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)
    [void]$bw.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
    $bw.Write([int](36 + $dataBytes))
    [void]$bw.Write([System.Text.Encoding]::ASCII.GetBytes("WAVE"))
    [void]$bw.Write([System.Text.Encoding]::ASCII.GetBytes("fmt "))
    $bw.Write([int]16); $bw.Write([int16]1); $bw.Write([int16]1)
    $bw.Write([int]$SR); $bw.Write([int]($SR * 2)); $bw.Write([int16]2); $bw.Write([int16]16)
    [void]$bw.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
    $bw.Write([int]$dataBytes)
    for ($i = 0; $i -lt $n; $i++) {
        $v = [int][math]::Round($Samples[$i] * 30000)
        if ($v -gt 32767) { $v = 32767 } elseif ($v -lt -32768) { $v = -32768 }
        $bw.Write([int16]$v)
    }
    $bw.Flush()
    [System.IO.File]::WriteAllBytes($Path, $ms.ToArray())
    $bw.Dispose(); $ms.Dispose()
    Write-Host "wrote $Path ($n frames, $([math]::Round($n / $SR, 2))s)"
}

# ---- village_bgm.wav : 8s looping pastoral tune ----
# Melody notes (semitone offsets from A4=440) over 16 beats, chords underneath (I V vi IV in C).
$bgmDur = 8.0
$nB = [int]($SR * $bgmDur)
$beats = 16
$beatLen = $nB / $beats
# C major pentatonic-ish melody line (MIDI note numbers).
$melody = @(72, 76, 79, 76, 74, 77, 72, 69, 72, 76, 81, 79, 77, 74, 72, 67)
# Chord roots per 4-beat bar: C(60) G(55) Am(57) F(53).
$chordRoots = @(60, 60, 60, 60, 55, 55, 55, 55, 57, 57, 57, 57, 53, 53, 53, 53)
function MidiHz([int]$m) { return 440.0 * [math]::Pow(2.0, ($m - 69) / 12.0) }

$bgm = New-Object double[] $nB
for ($i = 0; $i -lt $nB; $i++) {
    $beatIdx = [int]([math]::Floor($i / $beatLen)) % $beats
    $tInBeat = ($i - $beatIdx * $beatLen) / $beatLen
    # Melody: triangle-ish tone with a soft pluck envelope per beat.
    $mHz = MidiHz $melody[$beatIdx]
    $mEnv = [math]::Exp(-2.5 * $tInBeat)
    $mPhase = 2 * [math]::PI * $mHz * $i / $SR
    $tri = (2.0 / [math]::PI) * [math]::Asin([math]::Sin($mPhase))
    $lead = $tri * $mEnv * 0.34
    # Chord bed: soft sine on root + fifth, steady.
    $cHz = MidiHz $chordRoots[$beatIdx]
    $bed = ([math]::Sin(2 * [math]::PI * $cHz * $i / $SR) + `
            0.6 * [math]::Sin(2 * [math]::PI * ($cHz * 1.5) * $i / $SR)) * 0.12
    $s = $lead + $bed
    # Global loop fade at the very ends kept minimal so loop is near-seamless (phase continuous).
    $bgm[$i] = $s * 0.85
}
Write-Wav -Path (Join-Path $OutDir "village_bgm.wav") -Samples $bgm

# ---- footstep.wav : ~90ms decaying noise + thud (fixed seed) ----
$fsDur = 0.09
$nF = [int]($SR * $fsDur)
$rng = New-Object System.Random(1337)
$fs = New-Object double[] $nF
for ($i = 0; $i -lt $nF; $i++) {
    $t = $i / $nF
    $env = [math]::Exp(-6.0 * $t)
    $noise = ($rng.NextDouble() * 2.0 - 1.0)
    $tone = [math]::Sin(2 * [math]::PI * 140.0 * $i / $SR)
    $fs[$i] = ($noise * 0.5 + $tone * 0.5) * $env * 0.8
}
Write-Wav -Path (Join-Path $OutDir "footstep.wav") -Samples $fs

# ---- dialogue_blip.wav : ~50ms sine ping (text advance) ----
$blDur = 0.05
$nBl = [int]($SR * $blDur)
$bl = New-Object double[] $nBl
for ($i = 0; $i -lt $nBl; $i++) {
    $t = $i / $nBl
    $env = [math]::Sin([math]::PI * $t)         # smooth attack+release
    $bl[$i] = [math]::Sin(2 * [math]::PI * 880.0 * $i / $SR) * $env * 0.5
}
Write-Wav -Path (Join-Path $OutDir "dialogue_blip.wav") -Samples $bl

Write-Host "DONE audio"
