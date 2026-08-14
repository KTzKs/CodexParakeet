param(
    [Parameter(Position = 0)]
    [string]$RuntimeRoot = (Join-Path (Get-Location) 'runtime')
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$voicevoxSource = Join-Path $repositoryRoot 'third_party\voicevox_core'
$voicevoxDestination = Join-Path $RuntimeRoot 'voicevox_core'
$releaseOutput = Join-Path $repositoryRoot 'app_src\x64\Release'
$lipsyncSource = Join-Path $repositoryRoot 'assets\lipsync'
$lipsyncDestination = Join-Path $RuntimeRoot 'lipsync'
$notifyScript = Join-Path $PSScriptRoot 'codex_speak_notify.ps1'

$directories = @(
    $RuntimeRoot,
    (Join-Path $RuntimeRoot 'lipsync'),
    (Join-Path $RuntimeRoot 'voicevox_core'),
    (Join-Path $RuntimeRoot 'voicevox_core\c_api'),
    (Join-Path $RuntimeRoot 'voicevox_core\dict'),
    (Join-Path $RuntimeRoot 'voicevox_core\models'),
    (Join-Path $RuntimeRoot 'voicevox_core\onnxruntime')
)

foreach ($directory in $directories) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

if (-not (Test-Path -LiteralPath $voicevoxSource -PathType Container)) {
    throw "VOICEVOX CORE source was not found: $voicevoxSource"
}

Copy-Item -Path (Join-Path $voicevoxSource '*') `
    -Destination $voicevoxDestination -Recurse -Force

if (-not (Test-Path -LiteralPath $releaseOutput -PathType Container)) {
    throw "Release output was not found: $releaseOutput"
}

Copy-Item -Path (Join-Path $releaseOutput '*.exe') -Destination $RuntimeRoot -Force
Copy-Item -Path (Join-Path $releaseOutput '*.dll') -Destination $RuntimeRoot -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $notifyScript -Destination $RuntimeRoot -Force

# voicevox_core.dll is linked by the application and must sit beside the exe.
$voicevoxDll = Join-Path $voicevoxSource 'c_api\lib\voicevox_core.dll'
if (Test-Path -LiteralPath $voicevoxDll -PathType Leaf) {
    Copy-Item -LiteralPath $voicevoxDll -Destination $RuntimeRoot -Force
}

if (-not (Test-Path -LiteralPath $lipsyncSource -PathType Container)) {
    throw "Lipsync image directory was not found: $lipsyncSource"
}
Copy-Item -Path (Join-Path $lipsyncSource '*') -Destination $lipsyncDestination -Recurse -Force

Write-Host "Runtime folder structure created: $RuntimeRoot"
Write-Host "VOICEVOX CORE copied from: $voicevoxSource"
Write-Host "Release binaries copied from: $releaseOutput"
Write-Host "Lipsync images copied from: $lipsyncSource"
Write-Host "Notification script copied from: $notifyScript"
Write-Host ''
Write-Host 'Place the following files manually:'
Write-Host '  CodexParakeet.exe'
Write-Host '  CodexParakeet.ini'
Write-Host '  codex_speak_notify.ps1'
Write-Host '  voicevox_core.dll and mysofa.dll'
Write-Host '  lipsync images under lipsync\'
Write-Host '  VOICEVOX CORE files under voicevox_core\'
