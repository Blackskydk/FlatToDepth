@echo off
rem Double-click to set FlatToDepth up for the supported games found in your Steam library (see docs\GAMES.md).
rem Options (optional): -Games blindforest,wotw   -Yes   -AcceptFixLicense   -DryRun
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install.ps1" %*
echo.
pause
