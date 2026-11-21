<#
.SYNOPSIS
  Installs (or updates) the Frame Hands SteamVR driver.

.DESCRIPTION
  Copies the driver to %USERPROFILE%\FrameHands\framehands, registers it with SteamVR
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

# Not under AppData: SteamVR skips external drivers there ("not a directory"). Not in Documents
# either, which OneDrive often takes over.
$target = Join-Path $env:USERPROFILE "FrameHands\framehands"
$settings = Join-Path $target "resources\settings"

# Other places Frame Hands is registered (an older version's folder, a build folder): unregister
# them, and bring their settings along if this is the first install here.
$others = @()
foreach ($line in (& $vrpathreg show)) {
    if ($line -match '^\s*framehands\s*:\s*(.+?)\s*$' -and $Matches[1] -ne $target) { $others += $Matches[1] }
}
$oldAppData = Join-Path $env:LOCALAPPDATA "FrameHands\framehands"
if ((Test-Path $oldAppData) -and $others -notcontains $oldAppData) { $others += $oldAppData }

$keep = @{}
foreach ($dir in @($target) + $others) {
    $s = Join-Path $dir "resources\settings"
    if ($keep.Count -or -not (Test-Path $s)) { continue }
    foreach ($f in Get-ChildItem $s -Filter *.json) {
        if ($f.Name -eq "calibration.json" -or $f.Name -like "curl_range_*.json") { $keep[$f.Name] = [IO.File]::ReadAllBytes($f.FullName) }
    }
    if ($keep.Count -and $dir -ne $target) { Write-Host "Taking your settings from $dir" }
}
foreach ($dir in $others) {
    & $vrpathreg removedriver $dir | Out-Null
    Write-Host "Unregistered the old location: $dir"
}
if (Test-Path $oldAppData) { Remove-Item (Split-Path $oldAppData) -Recurse -Force }
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
