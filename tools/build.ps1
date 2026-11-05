<#
.SYNOPSIS
    Build DAOTransmog from any PowerShell shell.
.PARAMETER Preset
    debug or release
.PARAMETER Test
    Run unit tests after building.
.PARAMETER Fresh
    Clean build directory before configuring.
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string] $Preset = 'release',
    [switch] $Test,
    [switch] $Fresh
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Get-VcVarsAllPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $paths = & $vswhere -all -prerelease -products * -property installationPath
        foreach ($p in $paths) {
            $bat = Join-Path $p 'VC\Auxiliary\Build\vcvarsall.bat'
            if (Test-Path $bat) { return $bat }
        }
    }
    $fallback = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat'
    if (Test-Path $fallback) { return $fallback }
    throw "vcvarsall.bat not found"
}

function Import-VcVarsEnvironment {
    $bat = Get-VcVarsAllPath
    Write-Host "[DAOTransmog] Importing MSVC x86 environment from: $bat" -ForegroundColor Cyan
    $tempFile = [System.IO.Path]::GetTempFileName()
    cmd.exe /c "call `"$bat`" amd64_x86 >nul && set > `"$tempFile`""
    Get-Content $tempFile | ForEach-Object {
        if ($_ -match '^(.*?)=(.*)$') {
            Set-Content "env:\$($matches[1])" $matches[2]
        }
    }
    Remove-Item $tempFile -Force
}

# 1. Setup VS Environment
Import-VcVarsEnvironment

# 2. Add Ninja to PATH if not present
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $ninjaCandidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
    )
    foreach ($cand in $ninjaCandidates) {
        if (Test-Path (Join-Path $cand 'ninja.exe')) {
            $env:PATH = "$cand;$env:PATH"
            Write-Host "[DAOTransmog] Added Ninja to PATH: $cand" -ForegroundColor DarkGray
            break
        }
    }
}

# 3. Setup VCPKG_ROOT
if (-not $env:VCPKG_ROOT) {
    $vsVcpkg = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg'
    if (Test-Path $vsVcpkg) {
        $env:VCPKG_ROOT = $vsVcpkg
    }
}
Write-Host "[DAOTransmog] VCPKG_ROOT is: $env:VCPKG_ROOT" -ForegroundColor DarkGray

# 4. Auto-detect DAO_GAME_DIR if unset
if (-not $env:DAO_GAME_DIR) {
    $defaultGame = 'D:\Entertainment\BlackFlag\Dragon Age Origins'
    if (Test-Path (Join-Path $defaultGame 'bin_ship\DAOrigins.exe')) {
        $env:DAO_GAME_DIR = $defaultGame
        Write-Host "[DAOTransmog] Auto-detected DAO_GAME_DIR: $defaultGame" -ForegroundColor Cyan
    }
}

# 5. Generate .tlk localization files & UI ActionScript headers
Write-Host "[DAOTransmog] Updating localization talk tables..." -ForegroundColor Cyan
& python "$repo\tools\gen_tlk.py"
Write-Host "[DAOTransmog] Building Flash UI actions header..." -ForegroundColor Cyan
& python "$repo\tools\gen_ui_actions.py"
Write-Host "[DAOTransmog] Building Flash UI SWF overlay..." -ForegroundColor Cyan
& python "$repo\tools\build_swf.py"

# 6. Configure & Build
Push-Location $repo
try {
    $buildDir = Join-Path $repo "build\$Preset"
    if ($Fresh -and (Test-Path $buildDir)) {
        Write-Host "[DAOTransmog] Cleaning $buildDir..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $buildDir
    }

    Write-Host "[DAOTransmog] Configuring CMake ($Preset)..." -ForegroundColor Cyan
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

    Write-Host "[DAOTransmog] Building DAOTransmog ($Preset)..." -ForegroundColor Cyan
    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }

    if ($Test) {
        Write-Host "[DAOTransmog] Running tests..." -ForegroundColor Cyan
        ctest --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE" }
    }

    # 7. Deploy to Game Directory
    if ($env:DAO_GAME_DIR) {
        $gameBin = if (Test-Path (Join-Path $env:DAO_GAME_DIR 'DAOrigins.exe')) {
            $env:DAO_GAME_DIR
        } else {
            Join-Path $env:DAO_GAME_DIR 'bin_ship'
        }
        $dest = Join-Path $gameBin 'daolib\plugins\DAOTransmog'
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item -Force "$repo\build\$Preset\daolib\plugins\DAOTransmog\*" $dest
        Write-Host "[DAOTransmog] Deployed plugin to: $dest" -ForegroundColor Green
    }
}
finally {
    Pop-Location
}

Write-Host "[DAOTransmog] Build completed successfully!" -ForegroundColor Green
