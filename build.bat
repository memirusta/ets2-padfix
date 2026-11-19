@echo off
rem Builds build\padfix.dll, build\padfix_helper.exe and build\padfix_setup.exe (x64, static CRT).
rem Set PADFIX_VERSION=x.y.z to stamp a version into them (make_release.ps1 does this).
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

if not defined PADFIX_VERSION set "PADFIX_VERSION=0.0.0"
for /f "tokens=1-3 delims=." %%a in ("%PADFIX_VERSION%") do (
	set "PADFIX_VERSION_MAJOR=%%a"
	set "PADFIX_VERSION_MINOR=%%b"
	set "PADFIX_VERSION_PATCH=%%c"
)
> build\version.h (
	echo #ifndef PADFIX_VERSION_H
	echo #define PADFIX_VERSION_H
	echo #define PADFIX_VERSION_MAJOR %PADFIX_VERSION_MAJOR%
	echo #define PADFIX_VERSION_MINOR %PADFIX_VERSION_MINOR%
	echo #define PADFIX_VERSION_PATCH %PADFIX_VERSION_PATCH%
	echo #define PADFIX_VERSION_STRING "%PADFIX_VERSION%"
	echo #endif
)
rc /nologo /I build /I src /fo build\padfix.res src\padfix.rc || exit /b 1
rc /nologo /I build /I src /fo build\helper.res src\helper.rc || exit /b 1
rc /nologo /I build /I src /fo build\setup.res src\setup.rc || exit /b 1

cl /nologo /O2 /W3 /MT /TC /c third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c /Fobuild\minhook\ || exit /b 1
lib /nologo /OUT:build\minhook\minhook.lib build\minhook\buffer.obj build\minhook\hook.obj build\minhook\trampoline.obj build\minhook\hde64.obj || exit /b 1

cl /nologo /O2 /W4 /EHsc /MT /LD /I sdk\include src\padfix.cpp src\rumble_bridge.cpp /Fobuild\ /Fe:build\padfix.dll /link /DEF:src\padfix.def build\minhook\minhook.lib build\padfix.res || exit /b 1
cl /nologo /O2 /W4 /EHsc /MT src\helper.cpp /Fobuild\ /Fe:build\padfix_helper.exe /link /SUBSYSTEM:WINDOWS build\helper.res || exit /b 1
cl /nologo /O2 /W4 /EHsc /MT /I build src\setup.cpp /Fobuild\ /Fe:build\padfix_setup.exe /link /SUBSYSTEM:CONSOLE /MANIFEST:EMBED build\setup.res || exit /b 1

echo Build OK
