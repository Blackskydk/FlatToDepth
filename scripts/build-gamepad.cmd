@echo off
setlocal
cd /d "%~dp0.."
if not defined FLATTODEPTH_VS_INSTALL set "FLATTODEPTH_VS_INSTALL=C:\Program Files\Microsoft Visual Studio\18\Community"
call "%FLATTODEPTH_VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
if errorlevel 1 exit /b 1
set "FLATTODEPTH_SDK=%CD%\.deps\winsdk\c"
set "FLATTODEPTH_SDK_LIB=%CD%\.deps\winsdk-x86\c"
if exist "%FLATTODEPTH_SDK%\Include\10.0.26100.0\um\Windows.h" (
  set "INCLUDE=%FLATTODEPTH_SDK%\Include\10.0.26100.0\ucrt;%FLATTODEPTH_SDK%\Include\10.0.26100.0\shared;%FLATTODEPTH_SDK%\Include\10.0.26100.0\um;%INCLUDE%"
  set "LIB=%FLATTODEPTH_SDK_LIB%\um\x86;%FLATTODEPTH_SDK_LIB%\ucrt\x86;%LIB%"
  set "PATH=%FLATTODEPTH_SDK%\bin\10.0.26100.0\x64;%PATH%"
)
if not exist build\gamepad mkdir build\gamepad
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /LD /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Isrc src\xinput_proxy.cpp /Fobuild\gamepad\xinput_proxy.obj /link /DEF:src\xinput_proxy.def /OUT:build\gamepad\xinput9_1_0.dll /IMPLIB:build\gamepad\xinput_proxy.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Isrc tests\gamepad_client.cpp /Fobuild\gamepad\client.obj /Febuild\gamepad\FlatToDepthGamepadClient.exe
exit /b %errorlevel%
