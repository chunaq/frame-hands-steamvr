@echo off
rem Frame Hands: installs the SteamVR driver (close SteamVR first). Double-click it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-driver.ps1" %*
echo.
pause
