@echo off
rem Builds and runs regression checks, plus a read-only live input diagnostic.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "PADFIX_VSDIR="
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\padfix_test_vsdir.txt"
set /p PADFIX_VSDIR=<"%TEMP%\padfix_test_vsdir.txt"
del "%TEMP%\padfix_test_vsdir.txt" >nul 2>&1
if not defined PADFIX_VSDIR exit /b 1
call "%PADFIX_VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
if not exist build mkdir build
if not exist build\minhook\minhook.lib call build.bat || exit /b 1
cl /nologo /O2 /W4 /WX /EHsc /MT /I sdk\include tools\controller_merge_test.cpp /Fobuild\ /Fe:build\controller_merge_test.exe /link /SUBSYSTEM:CONSOLE || exit /b 1
pushd build
if /I "%~1"=="--sdk" (
    controller_merge_test.exe "%CD%\padfix.dll"
) else (
    controller_merge_test.exe
)
set "PADFIX_TEST_RESULT=%ERRORLEVEL%"
popd
if not "%PADFIX_TEST_RESULT%"=="0" exit /b %PADFIX_TEST_RESULT%
cl /nologo /O2 /W4 /WX /EHsc /MT tools\diagnose_input.cpp /Fobuild\ /Fe:build\diagnose_input.exe || exit /b 1
cl /nologo /O2 /W4 /WX /EHsc /MT /DPADFIX_RUMBLE_TEST tools\rumble_test.cpp src\rumble_bridge.cpp /Fobuild\ /Fe:build\rumble_test.exe /link build\minhook\minhook.lib || exit /b 1
build\rumble_test.exe || exit /b 1
echo Tests OK
