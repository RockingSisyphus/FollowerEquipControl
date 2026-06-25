@echo off
setlocal EnableExtensions

cd /D "%~dp0"

set "preset=Skyrim"
if not "%~1"=="" set "preset=%~1"

set "config=Release"
if not "%~2"=="" set "config=%~2"

if not defined VCPKG_ROOT (
  echo ERROR: VCPKG_ROOT is not set.
  echo Set VCPKG_ROOT to your vcpkg checkout folder, for example:
  echo   setx VCPKG_ROOT D:\dev\vcpkg
  exit /b 1
)

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo ERROR: vcpkg toolchain file was not found under VCPKG_ROOT:
  echo   "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
  exit /b 1
)

rem Avoid cmd.exe parsing issues with parentheses in "Program Files (x86)".
set "VSWHERE=%SystemDrive%\Progra~2\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" goto :have_vswhere

echo ERROR: vswhere.exe not found at "%VSWHERE%".
echo Install Visual Studio or Build Tools with the C++ workload.
exit /b 1

:have_vswhere
set "VSINSTALL="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo ERROR: No Visual Studio installation with VC tools was found.
  exit /b 1
)

set "VSDEVCMD=%VSINSTALL%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
  echo ERROR: VsDevCmd.bat not found under "%VSINSTALL%".
  exit /b 1
)

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 goto :failed

echo.
echo Configuring %preset%...
cmake -S . --preset "%preset%"
if errorlevel 1 goto :failed

echo.
echo Building %config%...
cmake --build build --config "%config%"
if errorlevel 1 goto :failed

echo.
echo BUILD SUCCESSFUL.
goto :end

:failed
set "EXITCODE=%ERRORLEVEL%"
echo.
echo BUILD FAILED. Exit code: %EXITCODE%
exit /b %EXITCODE%

:end
exit /b 0