@echo off
setlocal EnableDelayedExpansion

REM Change to the directory where this build.bat lives
cd /d "%~dp0"

REM ============================================
REM x64dbg-automate-plus — Build Script
REM Builds the x64dbg C++ plugin (x64 or Win32)
REM ============================================
REM
REM Usage:
REM   build.bat                   — Build 64-bit Release
REM   build.bat x86               — Build 32-bit Release
REM   build.bat x64 Debug         — Build 64-bit Debug
REM   build.bat x64 Release copy  — Build + copy to plugins dir
REM
REM Architecture:
REM   C++20 with MSVC toolchain v143 (VS 2022)
REM   Dependencies: cppzmq, msgpack-cxx (via vcpkg)
REM   Output: build64\Release\x64dbg-automate.dp64
REM ============================================

set "ARCH=%1"
set "CONFIG=%2"
set "ACTION=%3"

if "%ARCH%"=="" set "ARCH=x64"
if "%CONFIG%"=="" set "CONFIG=Release"

echo ============================================
echo x64dbg-automate-plus — Build Script
echo Architecture: %ARCH%
echo Configuration: %CONFIG%
echo ============================================
echo.

REM -------------------------------------------------------------
REM Step 1: Find vcvarsall.bat (VS Build Tools 2022)
REM -------------------------------------------------------------
set "VCVARSALL="

for %%V in (18 17 16) do (
    for %%E in (BuildTools;Community;Professional;Enterprise) do (
        if exist "C:\Program Files (x86)\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VCVARSALL=C:\Program Files (x86)\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat"
            goto :found_vcvars
        )
    )
)

REM Try Program Files (x64) as fallback
for %%V in (18 17 16) do (
    for %%E in (BuildTools;Community;Professional;Enterprise) do (
        if exist "C:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsall.bat"
            goto :found_vcvars
        )
    )
)

:found_vcvars
if "!VCVARSALL!"=="" (
    echo ERROR: Could not find vcvarsall.bat
    echo Install Visual Studio Build Tools 2022:
    echo   https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
    echo Select workload: "Desktop development with C++"
    pause
    exit /b 1
)

echo Found vcvarsall: !VCVARSALL!

REM -------------------------------------------------------------
REM Step 2: Set up MSVC environment
REM -------------------------------------------------------------
set "VCVARS_ARCH=%ARCH%"
if "%ARCH%"=="x86" set "VCVARS_ARCH=x86"
if "%ARCH%"=="Win32" set "VCVARS_ARCH=x86"

echo Setting up MSVC environment for !VCVARS_ARCH!...
call "!VCVARSALL!" !VCVARS_ARCH!
if errorlevel 1 (
    echo ERROR: Failed to set up MSVC environment
    pause
    exit /b 1
)

REM Verify MSVC is active
cl 2>&1 | findstr /C:"x86" >nul 2>&1
if not errorlevel 1 echo MSVC x86 confirmed
cl 2>&1 | findstr /C:"x64" >nul 2>&1
if not errorlevel 1 echo MSVC x64 confirmed

echo MSVC environment initialized (!VCVARS_ARCH!)
echo.

REM -------------------------------------------------------------
REM Step 3: Set vcpkg toolchain to our standalone installation
REM -------------------------------------------------------------
set "VCPKG_ROOT=C:\Dev\x64dbg_MCP_Automate_Plus\vcpkg"
set "VCPKG_TOOLCHAIN=!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake"
set "CMAKE_TOOLCHAIN_FILE=!VCPKG_TOOLCHAIN!"

REM Verify vcpkg toolchain exists
if not exist "!VCPKG_TOOLCHAIN!" (
    echo ERROR: vcpkg toolchain not found at !VCPKG_TOOLCHAIN!
    echo Run install-phase1-deps.bat first to set up dependencies.
    pause
    exit /b 1
)
echo Using vcpkg: !VCPKG_ROOT!
echo.

REM -------------------------------------------------------------
REM Step 4: Auto-detect VS generator and set CMake arch
REM -------------------------------------------------------------
REM Detect Visual Studio version from vcvarsall path
REM VS 2022 = version 17, VS 2026 = version 18, etc.
if exist "C:\Program Files (x86)\Microsoft Visual Studio\18\" (
    set "CMAKE_GEN=Visual Studio 18 2026"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\17\" (
    set "CMAKE_GEN=Visual Studio 17 2022"
) else (
    REM Fallback: let cmake auto-detect
    set "CMAKE_GEN="
)

if /I "%ARCH%"=="x64" (
    set "CMAKE_ARCH=x64"
    set "BUILD_DIR=build64"
) else (
    set "CMAKE_ARCH=Win32"
    set "BUILD_DIR=build32"
)

REM -------------------------------------------------------------
REM Step 4: Configure with CMake
REM -------------------------------------------------------------
echo ============================================
echo Step 1/3: Configuring with CMake...
echo ============================================
echo.

REM Point vcpkg to our standalone installation
set "VCPKG_ROOT=C:\Dev\x64dbg_MCP_Automate_Plus\vcpkg"

set "CMAKE_ARGS=-DCMAKE_TOOLCHAIN_FILE=!CMAKE_TOOLCHAIN_FILE! -DVCPKG_ROOT=!VCPKG_ROOT!"

if defined CMAKE_GEN (
    cmake -B "!BUILD_DIR!" -G "!CMAKE_GEN!" -A !CMAKE_ARCH! !CMAKE_ARGS!
) else (
    cmake -B "!BUILD_DIR!" -A !CMAKE_ARCH! !CMAKE_ARGS!
)
if errorlevel 1 (
    echo ERROR: CMake configure failed
    pause
    exit /b 1
)

echo.

REM -------------------------------------------------------------
REM Step 5: Build
REM -------------------------------------------------------------
echo ============================================
echo Step 2/3: Building (%CONFIG%)...
echo ============================================
echo.

cmake --build "!BUILD_DIR!" --config %CONFIG%
if errorlevel 1 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo Build successful!
echo Output: !BUILD_DIR!\%CONFIG%\x64dbg-automate.dp!CMAKE_ARCH:~-2!
echo.

REM -------------------------------------------------------------
REM Step 6: Copy to x64dbg plugins (optional)
REM -------------------------------------------------------------
if /I "%ACTION%"=="copy" (
    echo ============================================
    echo Step 3/3: Copying to x64dbg plugins...
    echo ============================================
    echo.
    
    REM Check X64DBG_PATH environment variable or use default
    REM X64DBG_PATH may be set to an .exe path (from MCP server config) — validate it
    REM must be a directory containing x64\ and x32\ subdirectories
    if defined X64DBG_PATH (
        if not exist "!X64DBG_PATH!\x64" set "X64DBG_PATH="
    )
    if not defined X64DBG_PATH (
        REM Try known installation paths — project-specific first
        if exist "C:\Dev\RE_Tools\snapshot_2025-08-19_19-40\release\x64" set "X64DBG_PATH=C:\Dev\RE_Tools\snapshot_2025-08-19_19-40\release"
        if not defined X64DBG_PATH if exist "C:\Program Files\x64dbg\x64" set "X64DBG_PATH=C:\Program Files\x64dbg"
        if not defined X64DBG_PATH if exist "C:\Program Files (x86)\x64dbg\x64" set "X64DBG_PATH=C:\Program Files (x86)\x64dbg"
        if not defined X64DBG_PATH if exist "C:\re\x64dbg_dev\release\x64" set "X64DBG_PATH=C:\re\x64dbg_dev\release"
    )
    
    if defined X64DBG_PATH (
        if /I "%ARCH%"=="x64" (
            set "PLUGIN_DIR=!X64DBG_PATH!\x64\plugins"
        ) else (
            set "PLUGIN_DIR=!X64DBG_PATH!\x32\plugins"
        )
        
        if not exist "!PLUGIN_DIR!" mkdir "!PLUGIN_DIR!"

        if /I "%ARCH%"=="x64" (
            copy /Y "!BUILD_DIR!\%CONFIG%\x64dbg-automate.dp64" "!PLUGIN_DIR!\" >nul
        ) else (
            copy /Y "!BUILD_DIR!\%CONFIG%\x64dbg-automate.dp32" "!PLUGIN_DIR!\" >nul
        )
        if exist "!BUILD_DIR!\%CONFIG%\libzmq-mt-4_3_5.dll" (
            copy /Y "!BUILD_DIR!\%CONFIG%\libzmq-mt-4_3_5.dll" "!PLUGIN_DIR!\" >nul
        )
        
        echo Plugin copied to: !PLUGIN_DIR!
    ) else (
        echo WARNING: X64DBG_PATH not found. Set X64DBG_PATH environment variable.
        echo   e.g. set X64DBG_PATH=C:\Program Files\x64dbg
    )
    echo.
)

echo ============================================
echo Build complete!
echo ============================================
echo.
echo Output: !BUILD_DIR!\%CONFIG%\x64dbg-automate.dp!CMAKE_ARCH:~-2!
if /I "%ACTION%"=="copy" echo Installed: !PLUGIN_DIR!
echo.

endlocal
