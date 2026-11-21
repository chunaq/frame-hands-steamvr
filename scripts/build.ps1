<#
.SYNOPSIS
  Builds the Frame Hands SteamVR driver and runs the tests.

.DESCRIPTION
  Needs Visual Studio 2022 (Desktop development with C++), CMake 3.20+ and git.
  The OpenVR SDK (Valve, BSD-3-Clause) is cloned into third_party/openvr at a fixed version the
  first time, unless -OpenVRDir points at a checkout you already have.

  Output: build\output\drivers\framehands  (install it with scripts\install-driver.ps1)

.EXAMPLE
  .\scripts\build.ps1
  .\scripts\build.ps1 -OpenVRDir C:\src\openvr -SkipTests
#>
param(
    [string]$OpenVRDir = "",
    [string]$BuildDir = "",
    [switch]$SkipTests
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$openvrTag = "v2.15.6"
$openvrCommit = "0924064316de3effbcd1acf1e309182a2deb1c05"
if (-not $BuildDir) { $BuildDir = Join-Path $root "build" }

if (-not $OpenVRDir) {
    $OpenVRDir = Join-Path $root "third_party\openvr"
    if (-not (Test-Path (Join-Path $OpenVRDir "headers\openvr_driver.h"))) {
        Write-Host "Getting the OpenVR SDK $openvrTag ..."
        git clone --depth 1 --branch $openvrTag https://github.com/ValveSoftware/openvr.git $OpenVRDir
        if ($LASTEXITCODE -ne 0) { throw "git clone of the OpenVR SDK failed" }
        $have = (git -C $OpenVRDir rev-parse HEAD).Trim()
        if ($have -ne $openvrCommit) { throw "OpenVR $openvrTag is commit $have, expected $openvrCommit" }
    }
}

# CMake: on PATH, or the one Visual Studio installs (found with vswhere).
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $c = Join-Path "$vs" "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if ($vs -and (Test-Path $c)) { $cmake = $c }
    }
}
if (-not $cmake) { throw "CMake not found: install Visual Studio 2022 with 'Desktop development with C++' (includes CMake)" }
$ctest = Join-Path (Split-Path $cmake) "ctest.exe"

& $cmake -S $root -B $BuildDir -G "Visual Studio 17 2022" -A x64 "-DOPENVR_DIR=$OpenVRDir"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& $cmake --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

if (-not $SkipTests) {
    & $ctest --test-dir $BuildDir -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
}

$driver = Join-Path $BuildDir "output\drivers\framehands"
Write-Host ""
Write-Host "Built: $driver"
Write-Host "Install it with: .\scripts\install-driver.ps1 -DriverDir `"$driver`""
