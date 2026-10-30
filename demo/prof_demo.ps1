# ==============================================================================
# RetroC - Live demonstration script (matches the pitch deck, slides "Live demo")
# ==============================================================================
# Usage:
#   powershell -ExecutionPolicy Bypass -File demo\prof_demo.ps1
#   powershell -ExecutionPolicy Bypass -File demo\prof_demo.ps1 -Auto
#   powershell -ExecutionPolicy Bypass -File demo\prof_demo.ps1 -Bench
# -Auto  : no pauses between steps
# -Bench : also run the live W1-10k benchmark at the end (about 50 s)
# ==============================================================================

param(
    [switch]$Auto = $false,
    [switch]$Bench = $false
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

# The MinGW build needs the MSYS2 UCRT64 runtime DLLs on PATH.
$Msys = "C:\msys64\ucrt64\bin"
if ((Test-Path $Msys) -and ($env:PATH -notlike "*$Msys*")) {
    $env:PATH = "$Msys;$env:PATH"
}

function Step-Pause {
    if (-not $Auto) {
        Write-Host ""
        Write-Host "Press [Enter] for the next step..." -ForegroundColor DarkGray
        [void][System.Console]::ReadLine()
    } else {
        Start-Sleep -Milliseconds 800
    }
}

function Print-Banner($title) {
    Write-Host ""
    Write-Host "======================================================================" -ForegroundColor Cyan
    Write-Host "  $title" -ForegroundColor Yellow
    Write-Host "======================================================================" -ForegroundColor Cyan
}

function Print-Note($text) {
    Write-Host "Watch for: " -ForegroundColor Magenta -NoNewline
    Write-Host $text -ForegroundColor Gray
}

function Run-Retroc {
    Write-Host ""
    Write-Host ("> retroc " + ($args -join " ")) -ForegroundColor Cyan
    & $Retroc @args
    if ($LASTEXITCODE -ne 0) {
        throw "retroc exited with code $LASTEXITCODE"
    }
}

$Retroc = "build\retroc.exe"
if (-not (Test-Path $Retroc)) { $Retroc = "build\Debug\retroc.exe" }
if (-not (Test-Path $Retroc)) {
    Write-Host "Error: retroc binary not found. Build first: cmake --build build" -ForegroundColor Red
    exit 1
}

Clear-Host
Print-Banner "RETROC: A COMPILER FOR DATAFLOW PIPELINES WHOSE PAST YOU CAN EDIT"
Write-Host "Swaraj Joshi, Johan Viji, Sudeep Kumar Sahu - CSE, VIT Vellore" -ForegroundColor DarkCyan
Write-Host "Pipeline: demo\pipeline.rc (average of valid sensor readings)" -ForegroundColor Gray
Get-Content "demo\pipeline.rc" | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }

Step-Pause

# ------------------------------------------------------------------------------
Print-Banner "STEP 1: COMPILE AND RUN"
Run-Retroc dump-ir demo\pipeline.rc
Run-Retroc run demo\pipeline.rc
Print-Note "one IR node per stage; the first result is the average of the 4 valid readings."

Step-Pause

# ------------------------------------------------------------------------------
Print-Banner "STEP 2: FOUR RETROACTIVE EDITS"
Get-Content "demo\edits.rcs" | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
Run-Retroc edit demo\pipeline.rc demo\edits.rcs
Print-Note "Edit #1 runs only the filter (1 of 5 ops) and changes no output: early cutoff."
Print-Note "Edit #2 sends one record through; Edits #3-#4 insert, then delete, a stage."

Step-Pause

# ------------------------------------------------------------------------------
Print-Banner "STEP 3: tree_reduce UPDATES ONE PATH TO THE ROOT"
Get-Content "demo\tree_demo.rc" | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
Run-Retroc edit demo\tree_demo.rc demo\tree_demo.rcs
Print-Note "changing 1 of 8 values evaluates 3 of 8 combines (log2 8 = 3)."

Step-Pause

# ------------------------------------------------------------------------------
Print-Banner "STEP 4: VERIFY AGAINST A FULL RERUN"
Run-Retroc verify demo\pipeline.rc demo\edits.rcs
Print-Note "the edited source is rewritten and run from scratch; the outputs must match exactly."

Step-Pause

# ------------------------------------------------------------------------------
Print-Banner "STEP 5: EXPLAIN WHY AN OUTPUT CHANGED"
Run-Retroc explain demo\pipeline.rc demo\edits.rcs avg_temp_f
Print-Note "Edit #1 is not listed: early cutoff showed it had no effect on avg_temp_f."

# ------------------------------------------------------------------------------
$RunBench = $Bench
if (-not $Bench -and -not $Auto) {
    Write-Host ""
    $answer = Read-Host "Run the live 10k-record benchmark now? It takes about 50 s [y/N]"
    $RunBench = ($answer -eq "y" -or $answer -eq "Y")
}
if ($RunBench) {
    Print-Banner "OPTIONAL: LIVE BENCHMARK, W1 WITH 10,000 RECORDS"
    if (-not (Test-Path "bench\data\w1_10000.rc")) {
        Write-Host "bench\data is missing; generate it first with build\gen_workload.exe" -ForegroundColor Red
    } else {
        $Out = Join-Path $env:TEMP "retroc_demo_bench"
        & "build\bench_runner.exe" bench\data $Out --only W1-10k --cpu 2
        Print-Note "B0/B2 is the speedup over a full rerun; Verify must say PASS on every row."
    }
}

Write-Host ""
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "  DEMO COMPLETE" -ForegroundColor Yellow
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host ""
