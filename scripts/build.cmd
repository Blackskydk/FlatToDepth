@echo off
setlocal
cd /d "%~dp0.."
if not defined FLATTODEPTH_VS_INSTALL set "FLATTODEPTH_VS_INSTALL=C:\Program Files\Microsoft Visual Studio\18\Community"
call "%FLATTODEPTH_VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
set "FLATTODEPTH_SDK=%CD%\.deps\winsdk\c"
set "FLATTODEPTH_SDK_LIB=%CD%\.deps\winsdk-x64\c"
if exist "%FLATTODEPTH_SDK%\Include\10.0.26100.0\um\Windows.h" (
  set "INCLUDE=%FLATTODEPTH_SDK%\Include\10.0.26100.0\ucrt;%FLATTODEPTH_SDK%\Include\10.0.26100.0\shared;%FLATTODEPTH_SDK%\Include\10.0.26100.0\um;%FLATTODEPTH_SDK%\Include\10.0.26100.0\winrt;%INCLUDE%"
  set "LIB=%FLATTODEPTH_SDK_LIB%\um\x64;%FLATTODEPTH_SDK_LIB%\ucrt\x64;%LIB%"
  set "PATH=%FLATTODEPTH_SDK%\bin\10.0.26100.0\x64;%PATH%"
)
set "FLATTODEPTH_CMAKE=%CD%\.deps\cmake\cmake-4.4.4-windows-x86_64\bin\cmake.exe"
if not exist "%FLATTODEPTH_CMAKE%" set "FLATTODEPTH_CMAKE=cmake"
if not defined FLATTODEPTH_BUILD_DIR set "FLATTODEPTH_BUILD_DIR=build"
"%FLATTODEPTH_CMAKE%" -S . -B "%FLATTODEPTH_BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo
if errorlevel 1 exit /b 1
"%FLATTODEPTH_CMAKE%" --build "%FLATTODEPTH_BUILD_DIR%"
exit /b %errorlevel%
