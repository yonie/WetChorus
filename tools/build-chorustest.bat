@echo off
REM Build and run chorustest - measures ChorusEngine with no plugin and no host.
REM Run from the plugin repo root:  tools\build-chorustest.bat
REM Mirrors WetEQ's tools\build-eqtest.bat, which existed while this did not.

set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
    echo ERROR: vcvars64.bat not found at "%VCVARS%"
    exit /b 1
)
call "%VCVARS%" >nul

if not exist build-tools mkdir build-tools

cl /nologo /EHsc /O2 /std:c++17 /I WetChorus\source ^
   /Fe:build-tools\chorustest.exe /Fo:build-tools\ ^
   tools\chorustest.cpp WetChorus\source\chorusengine.cpp
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo.
build-tools\chorustest.exe %*
exit /b %errorlevel%
