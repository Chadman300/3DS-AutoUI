$ErrorActionPreference = 'Stop'

$makeCandidates = @(
    'make',
    'C:\msys64\usr\bin\make.exe'
)
$gccCandidates = @(
    'gcc',
    'C:\msys64\mingw64\bin\gcc.exe'
)
$devkitRoot = if ($env:DEVKITPRO) { $env:DEVKITPRO } else { 'C:\devkitPro' }

$checks = @(
    @{ Name = 'make'; Paths = $makeCandidates; Required = $true },
    @{ Name = 'gcc'; Paths = $gccCandidates; Required = $true },
    @{ Name = 'devkitARM'; Paths = @((Join-Path $devkitRoot 'devkitARM\bin\arm-none-eabi-gcc.exe')); Required = $true },
    @{ Name = 'libctru'; Paths = @((Join-Path $devkitRoot 'libctru\include\3ds.h')); Required = $true },
    @{ Name = 'citro2d'; Paths = @((Join-Path $devkitRoot 'libctru\include\citro2d.h'), (Join-Path $devkitRoot 'libctru\lib\libcitro2d.a')); Required = $true }
)

$missing = @()
foreach ($check in $checks) {
    $ok = $false
    foreach ($path in $check.Paths) {
        if ($path -eq 'make' -or $path -eq 'gcc') {
            if (Get-Command $path -ErrorAction SilentlyContinue) { $ok = $true }
        }
        elseif (Test-Path $path) {
            $ok = $true
        }
    }

    if (-not $ok) {
        $missing += $check.Name
    }
}

if ($missing.Count -eq 0) {
    Write-Host 'Release environment looks ready for 3DS build.'
    exit 0
}

Write-Host 'Missing required 3DS toolchain components:'
foreach ($item in $missing) { Write-Host " - $item" }
Write-Host ''
Write-Host 'Install devkitPro / libctru / citro2d on a proper 3DS dev machine, then rerun this script.'
exit 1
