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
if not exist build\minhook mkdir build\minhook

cl /nologo /O2 /W3 /MT /TC /c third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c /Fobuild\minhook\ || exit /b 1
lib /nologo /OUT:build\minhook\minhook.lib build\minhook\buffer.obj build\minhook\hook.obj build\minhook\trampoline.obj build\minhook\hde64.obj || exit /b 1

cl /nologo /O2 /W4 /EHsc /MT /LD /I sdk\include src\padfix.cpp src\rumble_bridge.cpp /Fobuild\ /Fe:build\padfix.dll /link /DEF:src\padfix.def build\minhook\minhook.lib || exit /b 1
cl /nologo /O2 /W4 /EHsc /MT src\helper.cpp /Fobuild\ /Fe:build\padfix_helper.exe /link /SUBSYSTEM:WINDOWS || exit /b 1

echo Build OK
