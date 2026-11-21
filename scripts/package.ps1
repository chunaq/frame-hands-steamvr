<#
.SYNOPSIS
  Makes the release downloads from a build (scripts\build.ps1 first):
    dist\FrameHands-windows.zip       the SteamVR driver + PC install scripts (folder FrameHands-<version>)
    dist\frame-hands-headset.tar.gz   the headset exporter + installer
#>
param([string]$BuildDir = "")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root "build" }
$version = (Get-Content (Join-Path $root "VERSION") -Raw).Trim()
$driver = Join-Path $BuildDir "output\drivers\framehands"
if (-not (Test-Path (Join-Path $driver "bin\win64\driver_framehands.dll"))) { throw "Build first: scripts\build.ps1" }

$dist = Join-Path $root "dist"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
$pc = Join-Path $dist "FrameHands-$version"
New-Item -ItemType Directory -Force $pc, (Join-Path $pc "docs") | Out-Null
Copy-Item $driver (Join-Path $pc "framehands") -Recurse
Copy-Item (Join-Path $root "scripts\install-driver.ps1"), (Join-Path $root "scripts\uninstall-driver.ps1") $pc
Copy-Item (Join-Path $root "README.md"), (Join-Path $root "LICENSE"), (Join-Path $root "THIRD_PARTY_NOTICES.md") $pc
Copy-Item (Join-Path $root "docs\*.md") (Join-Path $pc "docs")
Compress-Archive -Path $pc -DestinationPath (Join-Path $dist "FrameHands-windows.zip")

$hs = Join-Path $dist "frame-hands-headset"
New-Item -ItemType Directory -Force $hs | Out-Null
Copy-Item (Join-Path $root "headset\*") $hs
Copy-Item (Join-Path $root "LICENSE") $hs
tar -czf (Join-Path $dist "frame-hands-headset.tar.gz") -C $dist "frame-hands-headset"
if ($LASTEXITCODE -ne 0) { throw "tar failed" }

Get-ChildItem $dist -File | ForEach-Object { "{0}  {1:N0} KB" -f $_.Name, ($_.Length / 1KB) }
