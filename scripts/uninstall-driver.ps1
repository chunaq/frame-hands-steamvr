<#
.SYNOPSIS
  Removes the Frame Hands SteamVR driver (and, as admin, its firewall rule). Close SteamVR first.
#>
param([switch]$KeepFiles)
$ErrorActionPreference = "Stop"
if (Get-Process vrserver, vrmonitor -ErrorAction SilentlyContinue) { throw "Close SteamVR first." }
$target = Join-Path $env:LOCALAPPDATA "FrameHands\framehands"
$steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
$vrpathreg = Get-ChildItem -Path @("$steam\steamapps\common\SteamVR\bin\win64", "${env:ProgramFiles(x86)}\Steam\steamapps\common\SteamVR\bin\win64") `
    -Filter vrpathreg.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ($vrpathreg) { & $vrpathreg.FullName removedriver $target; Write-Host "Unregistered from SteamVR" }
if (-not $KeepFiles -and (Test-Path $target)) { Remove-Item (Split-Path $target) -Recurse -Force; Write-Host "Removed $target" }
$rule = Get-NetFirewallRule -DisplayName "Frame Hands (SteamVR hand tracking, UDP 39877)" -ErrorAction SilentlyContinue
if ($rule) {
    try { $rule | Remove-NetFirewallRule; Write-Host "Removed the firewall rule" }
    catch { Write-Host "Run as admin to remove the firewall rule 'Frame Hands (SteamVR hand tracking, UDP 39877)'" }
}
