#Requires -Version 5.1
<#
.SYNOPSIS
    Build the x64dbg-automate-plus C++ plugin.

.DESCRIPTION
    Finds vcvarsall.bat, imports the MSVC environment into this PowerShell session,
    then runs cmake configure + build.  Optionally copies the output plugin to the
    x64dbg plugins directory.

.PARAMETER Arch
    Target architecture: x64 (default) or x86.

.PARAMETER Config
    Build configuration: Release (default) or Debug.

.PARAMETER Deploy
    Switch.  When set, copies the built plugin (and any required DLLs) to the
    matching x64dbg plugins directory.

.PARAMETER NoConfigure
    Switch.  Skip cmake configure; only rebuild (faster when CMakeCache exists).

.EXAMPLE
    # 64-bit Release build + deploy
    .\build.ps1 -Arch x64 -Deploy

.EXAMPLE
    # 32-bit Debug build, no copy
    .\build.ps1 -Arch x86 -Config Debug

.EXAMPLE
    # Rebuild only (skip configure)
    .\build.ps1 -NoConfigure
#>
[CmdletBinding()]
param(
    [ValidateSet('x64','x86','Win32')]
    [string]$Arch    = 'x64',

    [ValidateSet('Release','Debug')]
    [string]$Config  = 'Release',

    [switch]$Deploy,
    [switch]$NoConfigure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Normalise arch ────────────────────────────────────────────────────────────
if ($Arch -eq 'Win32') { $Arch = 'x86' }
$CMakeArch = if ($Arch -eq 'x64') { 'x64' } else { 'Win32' }
$BuildDir  = if ($Arch -eq 'x64') { 'build64' } else { 'build32' }
$PluginExt = if ($Arch -eq 'x64') { 'dp64' } else { 'dp32' }

# ── Change to script directory ────────────────────────────────────────────────
Push-Location $PSScriptRoot

try {

# ── Constants ─────────────────────────────────────────────────────────────────
$VcpkgRoot       = 'C:\Dev\x64dbg_MCP_Automate_Plus\vcpkg'
$VcpkgToolchain  = "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake"
$X64DbgRelease   = 'C:\Dev\RE_Tools\snapshot_2025-08-19_19-40\release'

# ── Banner ────────────────────────────────────────────────────────────────────
Write-Host '============================================' -ForegroundColor Cyan
Write-Host "x64dbg-automate-plus  Build Script (PS)"    -ForegroundColor Cyan
Write-Host "Architecture : $Arch"                        -ForegroundColor Cyan
Write-Host "Configuration: $Config"                      -ForegroundColor Cyan
Write-Host '============================================' -ForegroundColor Cyan
Write-Host ''

# ── Step 1: Verify vcpkg toolchain ────────────────────────────────────────────
Write-Host 'Checking vcpkg toolchain...'
if (-not (Test-Path $VcpkgToolchain)) {
    throw "vcpkg toolchain not found at $VcpkgToolchain`nRun install-phase1-deps.bat first."
}
Write-Host "  OK  $VcpkgToolchain" -ForegroundColor Green
Write-Host ''

# ── Step 2: Find vcvarsall.bat ────────────────────────────────────────────────
Write-Host 'Locating vcvarsall.bat...'
$VcVarsAll = $null

$vsVersions = @(18, 17, 16)
$vsEditions = @('BuildTools','Community','Professional','Enterprise')
$vsBases    = @(
    'C:\Program Files (x86)\Microsoft Visual Studio',
    'C:\Program Files\Microsoft Visual Studio'
)

:vsSearch foreach ($base in $vsBases) {
    foreach ($ver in $vsVersions) {
        foreach ($edition in $vsEditions) {
            $candidate = "$base\$ver\$edition\VC\Auxiliary\Build\vcvarsall.bat"
            if (Test-Path $candidate) {
                $VcVarsAll = $candidate
                break vsSearch
            }
        }
    }
}

if (-not $VcVarsAll) {
    throw "Could not find vcvarsall.bat.`nInstall Visual Studio Build Tools 2022 or later with 'Desktop development with C++'."
}
Write-Host "  OK  $VcVarsAll" -ForegroundColor Green
Write-Host ''

# ── Step 3: Import MSVC environment variables from vcvarsall.bat ──────────────
Write-Host "Setting up MSVC environment ($Arch)..."

$vcArg = if ($Arch -eq 'x64') { 'x64' } else { 'x86' }

# Ensure vswhere.exe (required by vcvarsall.bat) is on PATH for the child cmd session.
$vsInstallerDir = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
$childPath = if (Test-Path $vsInstallerDir) { "$env:PATH;$vsInstallerDir" } else { $env:PATH }

# Run vcvarsall + set in one cmd session.
# vcvarsall may emit vswhere warnings to stderr; suppress ErrorActionPreference
# around this call and rely on $LASTEXITCODE instead.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$rawEnv = cmd /c "set PATH=$childPath && `"$VcVarsAll`" $vcArg && set" 2>&1
$vcExit = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

if ($vcExit -ne 0) {
    throw "vcvarsall.bat failed (exit $vcExit)"
}

# Verify environment was actually initialized (look for INCLUDE or VCToolsInstallDir)
$envInitOk = $rawEnv | Where-Object { $_ -match '^(INCLUDE|VCToolsInstallDir)=' }
if (-not $envInitOk) {
    throw "vcvarsall.bat exited 0 but MSVC environment variables are missing - check the BuildTools installation."
}

$importCount = 0
foreach ($line in $rawEnv) {
    if ($line -match '^([^=]+)=(.*)$') {
        $varName  = $Matches[1].Trim()
        $varValue = $Matches[2]
        # Skip empty and PowerShell-reserved names
        if ($varName -and $varName -notmatch '^\s*$') {
            [System.Environment]::SetEnvironmentVariable($varName, $varValue, 'Process')
            $importCount++
        }
    }
}
Write-Host "  Imported $importCount environment variables" -ForegroundColor Green
Write-Host ''

# ── Step 4: Detect cmake ──────────────────────────────────────────────────────
Write-Host 'Locating cmake...'
$cmakeCmd = $null

# VS-bundled cmake locations (prefer bundled to avoid version mismatch)
$cmakeCandidates = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files (x86)\Microsoft Visual Studio\17\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\17\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
foreach ($c in $cmakeCandidates) {
    if (Test-Path $c) { $cmakeCmd = $c; break }
}

# Fall back to cmake on PATH
if (-not $cmakeCmd) {
    $found = Get-Command cmake -ErrorAction SilentlyContinue
    if ($found) { $cmakeCmd = $found.Source }
}

if (-not $cmakeCmd) {
    throw "cmake not found.  Install cmake or Visual Studio Build Tools (include CMake component)."
}
$cmakeVersion = & $cmakeCmd --version | Select-Object -First 1
Write-Host "  OK  $cmakeCmd" -ForegroundColor Green
Write-Host "      $cmakeVersion"   -ForegroundColor DarkGray
Write-Host ''

# ── Detect VS generator ───────────────────────────────────────────────────────
$CMakeGen = $null
if     ($VcVarsAll -match '\\18\\') { $CMakeGen = 'Visual Studio 18 2026' }
elseif ($VcVarsAll -match '\\17\\') { $CMakeGen = 'Visual Studio 17 2022' }
elseif ($VcVarsAll -match '\\16\\') { $CMakeGen = 'Visual Studio 16 2019' }

# ── Step 5: CMake configure ───────────────────────────────────────────────────
if (-not $NoConfigure) {
    Write-Host '============================================' -ForegroundColor Yellow
    Write-Host 'Step 1/3: Configuring with CMake...'         -ForegroundColor Yellow
    Write-Host '============================================' -ForegroundColor Yellow
    Write-Host ''

    $cmakeConfigArgs = @(
        '-B', $BuildDir,
        '-A', $CMakeArch,
        "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain",
        "-DVCPKG_ROOT=$VcpkgRoot"
    )
    if ($CMakeGen) {
        $cmakeConfigArgs = @('-G', $CMakeGen) + $cmakeConfigArgs
    }

    Write-Host "  cmake $cmakeConfigArgs" -ForegroundColor DarkGray
    & $cmakeCmd @cmakeConfigArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }
    Write-Host ''
}

# ── Step 6: Build ─────────────────────────────────────────────────────────────
Write-Host '============================================' -ForegroundColor Yellow
Write-Host "Step 2/3: Building ($Config)..."             -ForegroundColor Yellow
Write-Host '============================================' -ForegroundColor Yellow
Write-Host ''

& $cmakeCmd --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { throw "CMake build failed (exit $LASTEXITCODE)" }

$pluginPath = "$BuildDir\$Config\x64dbg-automate.$PluginExt"
if (-not (Test-Path $pluginPath)) {
    throw "Build appeared to succeed but output not found: $pluginPath"
}

Write-Host ''
Write-Host "Build successful!" -ForegroundColor Green
Write-Host "  Output: $pluginPath"
Write-Host ''

# ── Step 7: Deploy (optional) ─────────────────────────────────────────────────
if ($Deploy) {
    Write-Host '============================================' -ForegroundColor Yellow
    Write-Host 'Step 3/3: Deploying to x64dbg...'           -ForegroundColor Yellow
    Write-Host '============================================' -ForegroundColor Yellow
    Write-Host ''

    # Resolve x64dbg install dir (env var X64DBG_PATH takes priority if valid)
    $x64dbgBase = $null

    $envPath = [System.Environment]::GetEnvironmentVariable('X64DBG_PATH', 'Process')
    if ($envPath) {
        # Reject if X64DBG_PATH points to an executable or lacks x64 subdir
        if ((Test-Path "$envPath\x64") -and (Test-Path "$envPath\x32")) {
            $x64dbgBase = $envPath
        }
    }

    if (-not $x64dbgBase) {
        $candidates = @(
            $X64DbgRelease,
            'C:\Program Files\x64dbg',
            'C:\Program Files (x86)\x64dbg',
            'C:\re\x64dbg_dev\release'
        )
        foreach ($c in $candidates) {
            if ((Test-Path "$c\x64") -and (Test-Path "$c\x32")) {
                $x64dbgBase = $c; break
            }
        }
    }

    if (-not $x64dbgBase) {
        Write-Warning "x64dbg installation not found.  Set X64DBG_PATH to the release directory (must contain x64\ and x32\ subdirectories)."
    } else {
        $subDir    = if ($Arch -eq 'x64') { 'x64' } else { 'x32' }
        $pluginDir = "$x64dbgBase\$subDir\plugins"

        if (-not (Test-Path $pluginDir)) {
            New-Item -ItemType Directory -Path $pluginDir | Out-Null
        }

        Copy-Item $pluginPath $pluginDir -Force
        Write-Host "  Copied: $pluginPath" -ForegroundColor Green
        Write-Host "       -> $pluginDir"  -ForegroundColor Green

        # Copy companion DLL if present (ZMQ static build may leave one)
        $zmqDll = "$BuildDir\$Config\libzmq-mt-4_3_5.dll"
        if (Test-Path $zmqDll) {
            Copy-Item $zmqDll $pluginDir -Force
            Write-Host "  Copied: $zmqDll -> $pluginDir" -ForegroundColor Green
        }
        Write-Host ''
    }
}

# ── Done ──────────────────────────────────────────────────────────────────────
Write-Host '============================================' -ForegroundColor Cyan
Write-Host 'Build complete!'                             -ForegroundColor Cyan
Write-Host '============================================' -ForegroundColor Cyan
Write-Host ''
Write-Host "  Plugin : $BuildDir\$Config\x64dbg-automate.$PluginExt"
if ($Deploy -and $x64dbgBase) {
    $subDir    = if ($Arch -eq 'x64') { 'x64' } else { 'x32' }
    Write-Host "  Installed: $x64dbgBase\$subDir\plugins\"
}

} finally {
    Pop-Location
}
