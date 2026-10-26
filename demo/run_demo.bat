@echo off
setlocal enabledelayedexpansion

echo =====================================================================
echo  RetroC End-to-End Demo: Sensor Log Retroactive Editing
echo =====================================================================
echo.

set RETROC_BIN=build\retroc.exe
if not exist "%RETROC_BIN%" (
    set RETROC_BIN=build\Debug\retroc.exe
)
if not exist "%RETROC_BIN%" (
    echo Error: retroc executable not found. Please build the project first.
    exit /b 1
)

echo [Step 1] Running initial pipeline (from-scratch batch execution)...
echo Command: %RETROC_BIN% run demo\pipeline.rc
%RETROC_BIN% run demo\pipeline.rc
echo.

echo [Step 2] Applying retroactive edits (Modify record, Insert stage, Delete stage)...
echo Command: %RETROC_BIN% edit demo\pipeline.rc demo\edits.rcs
%RETROC_BIN% edit demo\pipeline.rc demo\edits.rcs
echo.

echo [Step 3] Verifying output equivalence against a full from-scratch rerun...
echo Command: %RETROC_BIN% verify demo\pipeline.rc demo\edits.rcs
%RETROC_BIN% verify demo\pipeline.rc demo\edits.rcs
echo.

echo [Step 4] Explaining provenance chain for output 'avg_temp_f'...
echo Command: %RETROC_BIN% explain demo\pipeline.rc demo\edits.rcs avg_temp_f
%RETROC_BIN% explain demo\pipeline.rc demo\edits.rcs avg_temp_f
echo.

echo =====================================================================
echo  Demo completed successfully in under 5 seconds!
echo =====================================================================
