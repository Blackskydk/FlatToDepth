@echo off
rem Opens the FlatToDepth game menu in the headset. Add this file to Steam once
rem (Steam > Games > Add a Non-Steam Game to My Library > Browse > All files > start-flattodepth.cmd)
rem and it can be started from the Steam library or the SteamVR dashboard like any game.
rem It stays in the foreground, so Steam shows it as running until you close it.
cd /d "%~dp0.."
if exist "bin\FlatToDepth.exe" ("bin\FlatToDepth.exe" --config "flattodepth.ini") else ("build\FlatToDepth.exe" --config "flattodepth.ini")
exit /b %errorlevel%
