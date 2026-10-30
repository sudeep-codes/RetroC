@echo off
setlocal
cd /d "%~dp0\.."

set AUTO_FLAG=
if "%1"=="--auto" set AUTO_FLAG=-Auto
if "%1"=="-auto" set AUTO_FLAG=-Auto
if "%1"=="-Auto" set AUTO_FLAG=-Auto
if "%1"=="--bench" set AUTO_FLAG=-Bench

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prof_demo.ps1" %AUTO_FLAG%
if errorlevel 1 (
    echo.
    echo Demo failed with error code %errorlevel%.
    pause
    exit /b %errorlevel%
)

echo.
pause
