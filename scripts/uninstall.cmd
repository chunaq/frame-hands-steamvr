@echo off
rem Frame Hands: removes the SteamVR driver (close SteamVR first). Double-click it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall-driver.ps1" %*
echo.
pause
