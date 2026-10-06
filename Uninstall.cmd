@echo off
rem Double-click to remove what Install.cmd added to your game folders.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\uninstall.ps1" %*
echo.
pause
