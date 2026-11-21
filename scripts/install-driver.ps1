<#
.SYNOPSIS
  Installs (or updates) the Frame Hands SteamVR driver.

.DESCRIPTION
  Copies the driver to %LOCALAPPDATA%\FrameHands\framehands, registers it with SteamVR
  (vrpathreg), and offers to allow its UDP port (39877) through Windows Firewall so the headset's
  hand data can reach it. On an update your settings (calibration.json) are kept.
  Close SteamVR first.

.EXAMPLE
  .\install-driver.ps1
  .\install-driver.ps1 -NoFirewall
#>
param(
    [string]$DriverDir = "",
    [switch]$NoFirewall,
    [switch]$FirewallOnly
)
$ErrorActionPreference = "Stop"
$port = 39877
$ruleName = "Frame Hands (SteamVR hand tracking, UDP $port)"

function Add-FrameHandsFirewallRule {
    if (Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue) {
        Write-Host "Firewall rule already there: $ruleName"
        return
    }
    New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Protocol UDP -LocalPort $port `
        -Action Allow -Profile Any | Out-Null
    Write-Host "Firewall: allowed incoming UDP $port ($ruleName)"
}

if ($FirewallOnly) { Add-FrameHandsFirewallRule; return }

# The driver folder: next to this script in a release zip, else the build output.
if (-not $DriverDir) {
    foreach ($c in @((Join-Path $PSScriptRoot "framehands"),
                     (Join-Path $PSScriptRoot "..\build\output\drivers\framehands"))) {
        if (Test-Path (Join-Path $c "driver.vrdrivermanifest")) { $DriverDir = (Resolve-Path $c).Path; break }
    }
}
if (-not $DriverDir -or -not (Test-Path (Join-Path $DriverDir "bin\win64\driver_framehands.dll"))) {
    throw "Driver folder not found. Pass -DriverDir <folder with driver.vrdrivermanifest and bin\win64\driver_framehands.dll>."
}

if (Get-Process vrserver, vrmonitor -ErrorAction SilentlyContinue) {
    throw "SteamVR is running. Close SteamVR, then run this again."
}

# SteamVR's vrpathreg: in Steam's library folders.
function Find-VrPathReg {
    $steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
    $libs = @()
    if ($steam) {
        $libs += $steam
        $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $libs += $m.Groups[1].Value -replace '\\', '\'
            }
        }
    }
    $libs += "${env:ProgramFiles(x86)}\Steam"
    foreach ($l in $libs | Select-Object -Unique) {
        $p = Join-Path $l "steamapps\common\SteamVR\bin\win64\vrpathreg.exe"
        if (Test-Path $p) { return $p }
    }
    return $null
}
$vrpathreg = Find-VrPathReg
if (-not $vrpathreg) { throw "SteamVR not found. Install SteamVR from Steam and start it once." }

$target = Join-Path $env:LOCALAPPDATA "FrameHands\framehands"
$settings = Join-Path $target "resources\settings"
$keep = @{}
if (Test-Path $settings) {
    foreach ($f in Get-ChildItem $settings -Filter *.json) {
        if ($f.Name -eq "calibration.json" -or $f.Name -like "curl_range_*.json") { $keep[$f.Name] = [IO.File]::ReadAllBytes($f.FullName) }
    }
}
if ((Resolve-Path $DriverDir).Path -ne $target) {
    if (Test-Path $target) { Remove-Item $target -Recurse -Force }
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Copy-Item $DriverDir $target -Recurse
}
foreach ($name in $keep.Keys) {
    if ($name -eq "calibration.json") {
        Copy-Item (Join-Path $settings $name) (Join-Path $settings "calibration.default.json") -Force
        Write-Host "Kept your calibration.json (this version's defaults: calibration.default.json)"
    }
    [IO.File]::WriteAllBytes((Join-Path $settings $name), $keep[$name])
}

& $vrpathreg adddriver $target
if ($LASTEXITCODE -ne 0) { throw "vrpathreg adddriver failed" }
Write-Host "Registered with SteamVR: $target"

if (-not $NoFirewall) {
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if ($admin) {
        Add-FrameHandsFirewallRule
    } else {
        $answer = Read-Host "Allow the headset's hand data through Windows Firewall (UDP $port)? Needs admin. [Y/n]"
        if ($answer -eq "" -or $answer -match '^[Yy]') {
            Start-Process powershell -Verb RunAs -Wait -ArgumentList @(
                "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-FirewallOnly")
        } else {
            Write-Host "Skipped. If no hands arrive, run: .\install-driver.ps1 -FirewallOnly (as admin)"
        }
    }
}

Write-Host ""
Write-Host "Done. Set up the headset (headset\install.sh), then start SteamVR."
Write-Host "Settings: $settings\calibration.json (changes apply while you play)"
