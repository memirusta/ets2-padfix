@echo off
rem Builds build\padfix.dll and build\padfix_helper.exe (x64, static CRT).
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
rem The "(x86)" in the path breaks for /f, so go through a temp file.
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\padfix_vsdir.txt"
set /p VSDIR=<"%TEMP%\padfix_vsdir.txt"
del "%TEMP%\padfix_vsdir.txt" >nul 2>&1
if not defined VSDIR (
	echo Visual Studio C++ tools not found.
	exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cd /d "%~dp0"
if not exist build mkdir build

cl /nologo /O2 /W4 /EHsc /MT /LD /I sdk\include src\padfix.cpp /Fobuild\ /Fe:build\padfix.dll /link /DEF:src\padfix.def || exit /b 1
cl /nologo /O2 /W4 /EHsc /MT src\helper.cpp /Fobuild\ /Fe:build\padfix_helper.exe /link /SUBSYSTEM:WINDOWS || exit /b 1

echo Build OK
