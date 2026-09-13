@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem SquareStar build entry point. No arguments opens the build menu.
rem -ExecutionPolicy Bypass applies only to each child PowerShell process started
rem below. It does not change the user's or machine's configured execution policy.

if /i "%~1"=="--help" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-entry.ps1" help
    exit /b %ERRORLEVEL%
)
if /i "%~1"=="/?" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-entry.ps1" help
    exit /b %ERRORLEVEL%
)

if "%~1"=="" goto :menu

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-entry.ps1" %*
exit /b %ERRORLEVEL%

:menu
echo.
echo SquareStar build   
echo ================
echo [1] Quick dev build  - incremental Release/NDEBUG, O2
echo [2] Release build    - O2, strict warnings, tests, portable package
echo [Q] Quit
echo.
set "SQUARESTAR_BUILD_CHOICE="
set /p "SQUARESTAR_BUILD_CHOICE=Enter 1 or 2: "

if "%SQUARESTAR_BUILD_CHOICE%"=="1" goto :quick
if "%SQUARESTAR_BUILD_CHOICE%"=="2" goto :release
if /i "%SQUARESTAR_BUILD_CHOICE%"=="Q" exit /b 0

echo.
echo Invalid choice. Enter 1, 2, or Q.
goto :menu

:quick
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-entry.ps1" fast
exit /b %ERRORLEVEL%

:release
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-entry.ps1" release
exit /b %ERRORLEVEL%
