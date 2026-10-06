@echo off
rem 64-bit twin of the input shim, for 64-bit games. The game's engine loads
rem XInput1_4.dll (older XInput names are tried as fallbacks), so the one DLL is built under both names.
setlocal
cd /d "%~dp0.."
if not defined FLATTODEPTH_VS_INSTALL set "FLATTODEPTH_VS_INSTALL=C:\Program Files\Microsoft Visual Studio\18\Community"
call "%FLATTODEPTH_VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
set "FLATTODEPTH_SDK=%CD%\.deps\winsdk\c"
set "FLATTODEPTH_SDK_LIB=%CD%\.deps\winsdk-x64\c"
if exist "%FLATTODEPTH_SDK%\Include\10.0.26100.0\um\Windows.h" (
  set "INCLUDE=%FLATTODEPTH_SDK%\Include\10.0.26100.0\ucrt;%FLATTODEPTH_SDK%\Include\10.0.26100.0\shared;%FLATTODEPTH_SDK%\Include\10.0.26100.0\um;%INCLUDE%"
  set "LIB=%FLATTODEPTH_SDK_LIB%\um\x64;%FLATTODEPTH_SDK_LIB%\ucrt\x64;%LIB%"
  set "PATH=%FLATTODEPTH_SDK%\bin\10.0.26100.0\x64;%PATH%"
)
if not exist build\gamepad64 mkdir build\gamepad64
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /LD /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Isrc src\xinput_proxy.cpp /Fobuild\gamepad64\xinput_proxy.obj /link /DEF:src\xinput_proxy64.def /OUT:build\gamepad64\xinput1_4.dll /IMPLIB:build\gamepad64\xinput_proxy.lib
if errorlevel 1 exit /b 1
copy /y build\gamepad64\xinput1_4.dll build\gamepad64\xinput1_3.dll >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Isrc tests\gamepad_client.cpp /Fobuild\gamepad64\client.obj /Febuild\gamepad64\FlatToDepthGamepadClient.exe
exit /b %errorlevel%
