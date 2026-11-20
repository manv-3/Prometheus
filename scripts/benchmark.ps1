<#
.SYNOPSIS
    PROMETHEUS benchmark harness — measures process CPU and memory footprint
    for adaptive vs fixed-complexity modes.

.DESCRIPTION
    Records process-level metrics (CPU %, private working set, working set)
    for a running prometheus_app.exe instance at 1 Hz for a configurable duration.
    Produces a JSON results file and a summary to stdout.

    Usage:
        # Adaptive run (normal operation):
        .\benchmark.ps1 -Mode adaptive -DurationSec 120 -WarmupSec 30

        # Fixed-complexity baseline (tier 3 pinned):
        .\benchmark.ps1 -Mode fixed -FixedTier 3 -DurationSec 120 -WarmupSec 30

    The app must already be running, OR use -Launch to start it automatically.
    When -Launch is used, the app is started with --synthetic so measurements
    are deterministic and not affected by real host variability.

    REQ-BENCH-01: records machine config, build info, process CPU (normalised
    by logical core count), private working set, and working set.

.NOTES
    Never presents synthetic-telemetry results as live measurements.
    Every output file is tagged with TelemetrySource.
#>

[CmdletBinding()]
param(
    # adaptive | fixed
    [ValidateSet("adaptive","fixed")]
    [string]$Mode = "adaptive",

    # Quality tier to pin (0=Dormant .. 4=Optimal). Only used when Mode=fixed.
    [ValidateRange(0,4)]
    [int]$FixedTier = 3,

    # Sampling duration after warmup (seconds)
    [int]$DurationSec = 120,

    # Warm-up period discarded from statistics (seconds)
    [int]$WarmupSec = 30,

    # Sampling interval (seconds)
    [int]$SampleIntervalSec = 1,

    # Launch the app automatically in --synthetic mode
    [switch]$Launch,

    # Path to prometheus_app.exe — required when -Launch is used
    [string]$AppPath = "$PSScriptRoot\..\build\windows-mingw-release\prometheus_app.exe",

    # Output directory for JSON results
    [string]$OutputDir = "$PSScriptRoot\..\docs\benchmarks",

    # Stress tool: path to stress-worker.ps1
    [string]$StressScript = "$PSScriptRoot\stress-worker.ps1",

    # Apply CPU stress during the benchmark run
    [switch]$WithStress,

    # Stress intensity 0.0–1.0 (fraction of logical cores to saturate)
    [ValidateRange(0.0,1.0)]
    [double]$StressIntensity = 0.5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# 0. Machine configuration record
# ---------------------------------------------------------------------------
$logicalCores = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
$cpu          = (Get-CimInstance Win32_Processor).Name | Select-Object -First 1
$ramGB        = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
$osVer        = (Get-CimInstance Win32_OperatingSystem).Caption
$osVer        = $osVer + " Build " + (Get-CimInstance Win32_OperatingSystem).BuildNumber
$machineId    = $env:COMPUTERNAME

$machineConfig = [ordered]@{
    machine       = $machineId
    cpu           = $cpu.Trim()
    logicalCores  = $logicalCores
    ramGB         = $ramGB
    os            = $osVer
    dateUtc       = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
}

Write-Host ""
Write-Host "=== PROMETHEUS Benchmark Harness ==="
Write-Host "Mode           : $Mode$(if ($Mode -eq 'fixed') { " (tier $FixedTier)" })"
Write-Host "Duration       : ${DurationSec}s (warmup ${WarmupSec}s discarded)"
Write-Host "Machine        : $machineId | $cpu | ${logicalCores} cores | ${ramGB} GB RAM"
Write-Host "OS             : $osVer"
Write-Host ""

# ---------------------------------------------------------------------------
# 1. Launch app if requested
# ---------------------------------------------------------------------------
$appProcess = $null
$launchedHere = $false

if ($Launch) {
    if (-not (Test-Path $AppPath)) {
        Write-Error "prometheus_app.exe not found at: $AppPath"
        exit 1
    }

    $launchArgs = @("--synthetic")  # Always synthetic when launched from benchmark
    if ($Mode -eq "fixed") {
        $launchArgs += "--fixed-tier"
        $launchArgs += "$FixedTier"
    }

    Write-Host "Launching: $AppPath $($launchArgs -join ' ')"
    Write-Host "NOTE: Running in --synthetic mode — telemetry is scripted, not live."
    Write-Host ""

    $appProcess = Start-Process -FilePath $AppPath -ArgumentList $launchArgs -PassThru
    $launchedHere = $true
    Start-Sleep -Seconds 3  # give the app time to start its threads
}

# ---------------------------------------------------------------------------
# 2. Find the running process
# ---------------------------------------------------------------------------
$procName = "prometheus_app"
$proc = Get-Process -Name $procName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) {
    Write-Error "No running process named '$procName' found. Use -Launch or start manually."
    if ($launchedHere -and $appProcess) { $appProcess.Kill() }
    exit 1
}
Write-Host "Monitoring PID $($proc.Id) ($procName)"

# ---------------------------------------------------------------------------
# 3. Start stress worker if requested
# ---------------------------------------------------------------------------
$stressJob = $null
if ($WithStress) {
    if (-not (Test-Path $StressScript)) {
        Write-Warning "Stress script not found at $StressScript — running without stress."
    } else {
        $stressCores = [math]::Max(1, [math]::Floor($logicalCores * $StressIntensity))
        Write-Host "Starting stress: $stressCores of $logicalCores cores at $([int]($StressIntensity*100))% intensity"
        $stressJob = Start-Job -FilePath $StressScript -ArgumentList $stressCores, ($DurationSec + $WarmupSec + 10)
    }
}

# ---------------------------------------------------------------------------
# 4. Collect samples
# ---------------------------------------------------------------------------
$samples = [System.Collections.Generic.List[object]]::new()
$totalSec = $WarmupSec + $DurationSec

Write-Host "Collecting $totalSec samples (1 Hz) — warmup: ${WarmupSec}s, measurement: ${DurationSec}s"
Write-Host ""

$startTime = Get-Date
$prevCpuSec = $proc.CPU
$prevWallTime = $startTime

for ($i = 0; $i -lt $totalSec; $i++) {
    Start-Sleep -Seconds $SampleIntervalSec

    # Refresh process data
    $proc.Refresh()
    if ($proc.HasExited) {
        Write-Warning "Process exited at sample $i — stopping collection."
        break
    }

    $now         = Get-Date
    $elapsed     = ($now - $startTime).TotalSeconds

    # CPU: delta CPU-seconds / delta wall-seconds / logical cores → 0–1 fraction per core
    $curCpuSec   = $proc.CPU
    $deltaWall   = ($now - $prevWallTime).TotalSeconds
    $deltaCpu    = $curCpuSec - $prevCpuSec
    $cpuFrac     = if ($deltaWall -gt 0) { $deltaCpu / $deltaWall / $logicalCores } else { 0 }
    $cpuFrac     = [math]::Max(0, [math]::Min(1, $cpuFrac))

    $prevCpuSec   = $curCpuSec
    $prevWallTime = $now

    $wsMB        = [math]::Round($proc.WorkingSet64 / 1MB, 2)
    $pvtMB       = [math]::Round($proc.PrivateMemorySize64 / 1MB, 2)
    $isWarmup    = ($i -lt $WarmupSec)

    $sample = [ordered]@{
        t_sec         = [math]::Round($elapsed, 1)
        cpu_norm      = [math]::Round($cpuFrac, 4)       # per logical core, [0,1]
        cpu_pct       = [math]::Round($cpuFrac * 100, 2) # percentage of one core
        working_set_mb     = $wsMB
        private_working_set_mb = $pvtMB
        warmup        = $isWarmup
    }
    $samples.Add($sample)

    $tag = if ($isWarmup) { "[warmup]" } else { "[record]" }
    Write-Host ("{0,4}s {1} CPU={2,5:F1}%  WS={3,6:F1}MB  Pvt={4,6:F1}MB" -f `
        [int]$elapsed, $tag, ($cpuFrac * 100), $wsMB, $pvtMB)
}

# ---------------------------------------------------------------------------
# 5. Stop stress
# ---------------------------------------------------------------------------
if ($stressJob) {
    Stop-Job $stressJob
    Remove-Job $stressJob -Force
}

# ---------------------------------------------------------------------------
# 6. Stop app if we launched it
# ---------------------------------------------------------------------------
if ($launchedHere -and $appProcess -and (-not $appProcess.HasExited)) {
    $appProcess.CloseMainWindow() | Out-Null
    Start-Sleep -Milliseconds 1000
    if (-not $appProcess.HasExited) { $appProcess.Kill() }
}

# ---------------------------------------------------------------------------
# 7. Statistics (measurement samples only — warmup excluded)
# ---------------------------------------------------------------------------
$measSamples = $samples | Where-Object { -not $_.warmup }

if ($measSamples.Count -eq 0) {
    Write-Error "No measurement samples collected."
    exit 1
}

$cpuValues  = $measSamples | ForEach-Object { $_.cpu_pct }
$wsValues   = $measSamples | ForEach-Object { $_.working_set_mb }
$pvtValues  = $measSamples | ForEach-Object { $_.private_working_set_mb }

function Get-Stats($values) {
    $sorted = $values | Sort-Object
    $n      = $sorted.Count
    $avg    = ($sorted | Measure-Object -Average).Average
    $min    = $sorted[0]
    $max    = $sorted[$n - 1]
    $p50    = $sorted[[int]($n * 0.50)]
    $p95    = $sorted[[int]($n * 0.95)]
    return [ordered]@{ avg=$([math]::Round($avg,2)); min=$min; p50=$p50; p95=$p95; max=$max; n=$n }
}

$cpuStats = Get-Stats $cpuValues
$wsStats  = Get-Stats $wsValues
$pvtStats = Get-Stats $pvtValues

# ---------------------------------------------------------------------------
# 8. Build result object
# ---------------------------------------------------------------------------

# Get build info
$gitHash = (git -C $PSScriptRoot rev-parse --short HEAD 2>$null) -join ""
$gitDirty = if ((git -C $PSScriptRoot status --porcelain 2>$null) -ne "") { $true } else { $false }

$result = [ordered]@{
    meta = [ordered]@{
        benchmark_version = "1.0"
        prometheus_commit = $gitHash
        build_dirty       = $gitDirty
        mode              = $Mode
        fixed_tier        = if ($Mode -eq "fixed") { $FixedTier } else { $null }
        telemetry         = "synthetic"   # always synthetic when run from this harness
        with_stress       = $WithStress.IsPresent
        stress_intensity  = if ($WithStress) { $StressIntensity } else { $null }
        warmup_sec        = $WarmupSec
        duration_sec      = $DurationSec
        sample_count      = $measSamples.Count
    }
    machine = $machineConfig
    summary = [ordered]@{
        cpu_pct            = $cpuStats
        working_set_mb     = $wsStats
        private_working_set_mb = $pvtStats
    }
    samples = @($samples)
}

# ---------------------------------------------------------------------------
# 9. Write output file
# ---------------------------------------------------------------------------
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$timestamp = (Get-Date).ToString("yyyy-MM-dd_HH-mm-ss")
$outFile   = Join-Path $OutputDir "${timestamp}_${Mode}$(if ($Mode -eq 'fixed') { "_tier$FixedTier" }).json"

$result | ConvertTo-Json -Depth 10 | Set-Content -Path $outFile -Encoding UTF8

# ---------------------------------------------------------------------------
# 10. Print summary
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "=== Results ($Mode mode — $($measSamples.Count) samples, ${DurationSec}s) ==="
Write-Host ("CPU (%/core)         avg={0,6:F2}  p50={1,6:F2}  p95={2,6:F2}  max={3,6:F2}" -f `
    $cpuStats.avg, $cpuStats.p50, $cpuStats.p95, $cpuStats.max)
Write-Host ("Working Set (MB)     avg={0,6:F1}  p50={1,6:F1}  p95={2,6:F1}  max={3,6:F1}" -f `
    $wsStats.avg, $wsStats.p50, $wsStats.p95, $wsStats.max)
Write-Host ("Private WS (MB)      avg={0,6:F1}  p50={1,6:F1}  p95={2,6:F1}  max={3,6:F1}" -f `
    $pvtStats.avg, $pvtStats.p50, $pvtStats.p95, $pvtStats.max)
Write-Host ""
Write-Host "Output: $outFile"
Write-Host ""
Write-Host "NOTE: All measurements are SYNTHETIC telemetry (--synthetic mode)."
Write-Host "      Label clearly as synthetic in any report or submission."
