param(
    [ValidateSet('debug', 'release', 'clean')]
    [string]$Configuration = 'release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$devkitRoot = if ($env:DEVKITPRO) { $env:DEVKITPRO } else { 'C:\devkitPro' }
$make = Get-Command make -ErrorAction SilentlyContinue

if (-not $make -and (Test-Path 'C:\msys64\usr\bin\make.exe')) {
    $make = Get-Item 'C:\msys64\usr\bin\make.exe'
}

if (-not $make) {
    throw 'GNU make was not found. Install MSYS2 or devkitPro build tools first.'
}

$makePath = if ($make -is [System.IO.FileInfo]) { $make.FullName } else { $make.Source }
$msysBin = 'C:\msys64\usr\bin'
if (Test-Path $msysBin) {
    $env:PATH = "$msysBin;$env:PATH"
}

$env:DEVKITPRO = if ($devkitRoot -match '^C:\\') { '/c/' + $devkitRoot.Substring(3).Replace('\', '/') } else { $devkitRoot }
$env:DEVKITARM = "$($env:DEVKITPRO)/devkitARM"

$projectPath = Join-Path $repoRoot '3ds_app'
$target = if ($Configuration -eq 'clean') { 'clean' } else { 'all' }

if ($Configuration -eq 'release') {
    & $makePath -C $projectPath clean
    if ($LASTEXITCODE -ne 0) {
        throw "3DS clean failed with exit code $LASTEXITCODE."
    }
}

& $makePath -C $projectPath $target
if ($LASTEXITCODE -ne 0) {
    throw "3DS build failed with exit code $LASTEXITCODE. Check DEVKITPRO=$($env:DEVKITPRO)."
}

if ($Configuration -ne 'clean') {
    $artifact = Join-Path $repoRoot '3ds_app\3ds_autoui.3dsx'
    if (-not (Test-Path $artifact)) {
        throw "Build reported success but artifact was not found: $artifact"
    }
    Write-Host "3DS artifact: $artifact"
}