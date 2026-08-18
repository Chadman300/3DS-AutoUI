param(
    [string]$DevkitRoot = 'C:\devkitPro'
)

$ErrorActionPreference = 'Stop'

Write-Host 'This script is a best-effort helper for preparing a Windows 3DS homebrew dev environment.'
Write-Host 'Official devkitPro installation is still required for a fully supported build.'
Write-Host ''
Write-Host 'Recommended steps:'
Write-Host '  1. Install MSYS2'
Write-Host '  2. Install devkitPro packages from the official devkitPro setup tools'
Write-Host '  3. Ensure DEVKITPRO and DEVKITARM are set in your environment'
Write-Host '  4. Run make -C 3ds_app from the repo root'
Write-Host ''

if (-not (Test-Path $DevkitRoot)) {
    Write-Host "No devkitPro install found at $DevkitRoot" 
    Write-Host 'Install the official devkitPro toolchain on this machine, then rerun the release check.'
    exit 1
}

$env:DEVKITPRO = $DevkitRoot
$env:DEVKITARM = Join-Path $DevkitRoot 'devkitARM'

Write-Host "DEVKITPRO=$($env:DEVKITPRO)"
Write-Host "DEVKITARM=$($env:DEVKITARM)"

if (Test-Path (Join-Path $env:DEVKITARM 'bin')) {
    Write-Host 'devkitARM appears installed.'
    exit 0
}

Write-Host 'devkitARM was not found. Install the official devkitPro toolchain first.'
exit 1
