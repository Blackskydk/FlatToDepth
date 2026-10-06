@echo off
rem Steam launch option for any game in the catalog: starts the VR bridge in the background, then runs the game as usual.
rem In Steam: the game > Properties > General > Launch Options, then paste:
rem   "<this folder>\launch-game.cmd" %command%
rem The bridge only starts when SteamVR is running (and none is up yet) and exits by itself when the game does.
rem A problem with the bridge never stops the game from starting; see logs\launch.log.
start "" /b powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-bridge.ps1" >nul 2>&1
%*
exit /b %errorlevel%
