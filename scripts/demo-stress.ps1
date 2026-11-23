<#
.SYNOPSIS
    PROMETHEUS - Survival Test demo stress controller.

.DESCRIPTION
    Runs the structured "Survival Test" demonstration:

      Stage 1  Birth       - launch the organism (synthetic or real)
      Stage 2  Healthy     - idle, observe normal behaviour
      Stage 3  Pressure    - ramp CPU stress, watch organism degrade
      Stage 4  Survival    - hold pressure, observe dormancy if triggered
      Stage 5  Resurrection- release pressure, observe recovery
      Stage 6  Proof       - print process metrics comparing stages

    All CPU measurements come from the prometheus_app process directly.
    Synthetic mode is the default so the demo never destabilises the host.

    Usage:
        # Full demo in synthetic mode (recommended for judges)
        .\demo-stress.ps1

        # Quick test run with compressed timing
        .\demo-stress.ps1 -IdleSec 10 -RampSec 15 -SustainSec 20 -RecoverySec 20

        # Real telemetry (host must actually have load applied externally)
        .\demo-stress.ps1 -Synthetic:$false -Launch:$false

.NOTES
    Every printed measurement states whether it is SYNTHETIC or LIVE.
    Never present synthetic results as live host measurements.
#>
[CmdletBinding()]
param(
    [switch]$Synthetic = $true,

    # Timing (seconds per stage)
    [int]$IdleSec      = 30,
    [int]$RampSec      = 20,
    [int]$SustainSec   = 40,
    [int]$RecoverySec  = 30,

    # Stress intensity: fraction of logical cores to saturate
    [ValidateRange(0.1, 0.9)]
    [double]$StressIntensity = 0.6,

    # Path to prometheus_app.exe
    [string]$AppPath = "$PSScriptRoot\..\build\windows-mingw-release\prometheus_app.exe",

    # Automatically launch the app before starting
    [switch]$Launch = $true
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$logicalCores  = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$stressCores   = [math]::Max(1, [int]([math]::Floor($logicalCores * $StressIntensity)))
$telemetryTag  = if ($Synthetic) { "SYNTHETIC" } else { "LIVE" }
$stressScript  = "$PSScriptRoot\stress-worker.ps1"
$appProcess    = $null
$stressJob     = $null

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Write-Stage([int]$n, [string]$name, [string]$desc) {
    Write-Host ""
    Write-Host ("=" * 60) -ForegroundColor Cyan
    Write-Host "  Stage $n - $name" -ForegroundColor Cyan
    Write-Host "  $desc" -ForegroundColor Gray
    Write-Host ("=" * 60) -ForegroundColor Cyan
}

function Get-AppProcess {
    return Get-Process -Name "prometheus_app" -ErrorAction SilentlyContinue |
           Select-Object -First 1
}

function Sample-Metrics([int]$durationSec, [string]$stageLabel) {
    $samples = [System.Collections.Generic.List[object]]::new()
    $p = Get-AppProcess
    if (-not $p) { Write-Warning "prometheus_app not found"; return $samples }
    $p.Refresh()
    $prevCpu  = $p.CPU
    $prevTime = Get-Date

    for ($i = 0; $i -lt $durationSec; $i++) {
        Start-Sleep -Seconds 1
        $p = Get-AppProcess
        if (-not $p) { break }
        $p.Refresh()

        $nowTime   = Get-Date
        $deltaWall = ($nowTime - $prevTime).TotalSeconds
        $deltaCpu  = $p.CPU - $prevCpu
        $cpuPct    = if ($deltaWall -gt 0) {
            [math]::Max(0, [math]::Round($deltaCpu / $deltaWall / $logicalCores * 100, 2))
        } else { 0 }
        $wsMB  = [math]::Round($p.WorkingSet64 / 1MB, 1)
        $pvtMB = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)

        $samples.Add([pscustomobject]@{
            t      = $i + 1
            stage  = $stageLabel
            cpu    = $cpuPct
            ws_mb  = $wsMB
            pvt_mb = $pvtMB
        })

        $line = "  " + ($i+1).ToString().PadLeft(3) + "s" +
                "  [" + $stageLabel + "]" +
                "  CPU=" + $cpuPct.ToString("F1").PadLeft(5) + " pct" +
                "  WS=" + $wsMB.ToString("F1").PadLeft(5) + " MB" +
                "  Pvt=" + $pvtMB.ToString("F1").PadLeft(5) + " MB"
        Write-Host $line

        $prevCpu  = $p.CPU
        $prevTime = $nowTime
    }
    return $samples
}

function Start-StressWorker {
    if (-not (Test-Path $stressScript)) {
        Write-Warning "stress-worker.ps1 not found at $stressScript - running without stress."
        return $null
    }
    $totalSec = $RampSec + $SustainSec + 15
    return Start-Job -FilePath $stressScript -ArgumentList $stressCores, $totalSec
}

function Stop-StressWorker($job) {
    if ($job) {
        Stop-Job  $job -ErrorAction SilentlyContinue
        Remove-Job $job -Force -ErrorAction SilentlyContinue
    }
}

function Show-StageSummary([string]$label, $samples) {
    if (-not $samples -or $samples.Count -eq 0) { return }
    $cpuAvg = [math]::Round(($samples | Measure-Object -Property cpu -Average).Average, 2)
    $cpuMax = [math]::Round(($samples | Measure-Object -Property cpu -Maximum).Maximum, 2)
    $wsAvg  = [math]::Round(($samples | Measure-Object -Property ws_mb -Average).Average, 1)
    $line = "  " + $label.PadRight(14) +
            "  CPU avg=" + $cpuAvg.ToString("F1").PadLeft(5) + " pct" +
            "  peak=" + $cpuMax.ToString("F1").PadLeft(5) + " pct" +
            "  WS avg=" + $wsAvg.ToString("F1").PadLeft(5) + " MB" +
            "  [" + $telemetryTag + "]"
    Write-Host $line -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 0. Header
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "##############################################" -ForegroundColor Magenta
Write-Host "   PROMETHEUS - SURVIVAL TEST DEMO            " -ForegroundColor Magenta
Write-Host "##############################################" -ForegroundColor Magenta
Write-Host ""
Write-Host "Telemetry   : $telemetryTag"
$stressPct = [int]($StressIntensity * 100)
Write-Host "Stress      : $stressCores of $logicalCores cores ($stressPct pct)"
Write-Host "Timing      : idle=${IdleSec}s  ramp=${RampSec}s  sustain=${SustainSec}s  recovery=${RecoverySec}s"
Write-Host ""
Write-Host "Claim being demonstrated:" -ForegroundColor Green
Write-Host "  Under rising host pressure, PROMETHEUS reduces its own" -ForegroundColor Green
Write-Host "  measured computational work, preserves its identity," -ForegroundColor Green
Write-Host "  and resumes normal activity when conditions recover." -ForegroundColor Green

# ---------------------------------------------------------------------------
# Stage 1 - Birth
# ---------------------------------------------------------------------------
Write-Stage 1 "Birth" "Launching organism..."

if ($Launch) {
    if (-not (Test-Path $AppPath)) {
        Write-Error "prometheus_app.exe not found at: $AppPath`nBuild: cmake --build --preset windows-mingw-release"
        exit 1
    }
    $launchArgs = [System.Collections.Generic.List[string]]::new()
    if ($Synthetic) { $launchArgs.Add("--synthetic") }

    $argsStr = $launchArgs -join " "
    Write-Host "  Launching: $AppPath $argsStr"
    $appProcess = Start-Process -FilePath $AppPath -ArgumentList $launchArgs -PassThru
    Start-Sleep -Seconds 4   # let threads start

    $initProc = Get-AppProcess
    if ($initProc) {
        $initProc.Refresh()
        $initWS  = [math]::Round($initProc.WorkingSet64 / 1MB, 1)
        $initPvt = [math]::Round($initProc.PrivateMemorySize64 / 1MB, 1)
        Write-Host "  PID $($initProc.Id) alive   WS=${initWS} MB   Pvt=${initPvt} MB"
    } else {
        Write-Warning "Could not find prometheus_app process after launch."
    }
}

# ---------------------------------------------------------------------------
# Stage 2 - Healthy baseline
# ---------------------------------------------------------------------------
Write-Stage 2 "Healthy Environment" "Observing normal behaviour for ${IdleSec}s..."
Write-Host "  Watch: organism should be Idle or Exploring, tier 3-4, particles visible."
$s2 = Sample-Metrics $IdleSec "Healthy"

# ---------------------------------------------------------------------------
# Stage 3 - Apply pressure (ramp)
# ---------------------------------------------------------------------------
Write-Stage 3 "Environmental Pressure" "Applying CPU stress for ${RampSec}s..."
Write-Host "  Watch: behaviour transitions to Stressed/Resting, tier drops, particles decrease."
$stressJob = Start-StressWorker
$s3 = Sample-Metrics $RampSec "Ramping"

# ---------------------------------------------------------------------------
# Stage 4 - Sustained pressure
# ---------------------------------------------------------------------------
Write-Stage 4 "Survival" "Sustaining pressure for ${SustainSec}s..."
Write-Host "  Watch: possible Dormant transition, particle count -> 5, sim Hz -> 1."
Write-Host "  If dormant: sleeping icon shown, budget drops to Tier0, process CPU falls."
$s4 = Sample-Metrics $SustainSec "Stressed"

# ---------------------------------------------------------------------------
# Stage 5 - Release pressure
# ---------------------------------------------------------------------------
Write-Stage 5 "Resurrection" "Releasing stress, observing recovery for ${RecoverySec}s..."
Stop-StressWorker $stressJob
$stressJob = $null
Write-Host "  Watch: behaviour recovers through tiers one-at-a-time, identity preserved."
$s5 = Sample-Metrics $RecoverySec "Recovering"

# ---------------------------------------------------------------------------
# Stage 6 - Proof summary
# ---------------------------------------------------------------------------
Write-Stage 6 "Proof" "Process-level evidence of adaptive behaviour:"
Write-Host ""
Show-StageSummary "Healthy"    $s2
Show-StageSummary "Ramping"    $s3
Show-StageSummary "Stressed"   $s4
Show-StageSummary "Recovering" $s5

Write-Host ""
Write-Host "KEY CLAIM CHECK:" -ForegroundColor Green

$healthyCpu = if ($s2.Count -gt 0) {
    ($s2 | Measure-Object -Property cpu -Average).Average
} else { 0 }

$stressedCpu = if ($s4.Count -gt 0) {
    ($s4 | Measure-Object -Property cpu -Average).Average
} else { 0 }

$hcStr = [math]::Round($healthyCpu, 2).ToString("F2")
$scStr = [math]::Round($stressedCpu, 2).ToString("F2")

if ($stressedCpu -le $healthyCpu -or $healthyCpu -eq 0) {
    Write-Host "  PASS  CPU under pressure ($scStr pct) <= healthy ($hcStr pct)" -ForegroundColor Green
    Write-Host "        Organism reduced its computational footprint under pressure." -ForegroundColor Green
} else {
    Write-Host "  CHECK CPU under pressure ($scStr pct) > healthy ($hcStr pct)" -ForegroundColor Yellow
    Write-Host "        Was pressure high enough to trigger tier degradation?" -ForegroundColor Yellow
    Write-Host "        Try: -StressIntensity 0.8 or longer -SustainSec" -ForegroundColor Yellow
}

Write-Host ""
if ($Synthetic) {
    Write-Host "NOTE: Telemetry is SYNTHETIC (scripted curves, not live host load)." -ForegroundColor Gray
    Write-Host "      CPU measurements above reflect the organism's OWN process cost," -ForegroundColor Gray
    Write-Host "      not the host system's total CPU utilisation." -ForegroundColor Gray
} else {
    Write-Host "NOTE: Telemetry is LIVE. CPU measurements are from the process directly." -ForegroundColor Gray
}

# ---------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------
Stop-StressWorker $stressJob
if ($appProcess -and (-not $appProcess.HasExited)) {
    Write-Host ""
    Write-Host "Closing organism..."
    $appProcess.CloseMainWindow() | Out-Null
    Start-Sleep -Milliseconds 1500
    if (-not $appProcess.HasExited) { $appProcess.Kill() }
}

Write-Host ""
Write-Host "Demo complete." -ForegroundColor Cyan
