<#
.SYNOPSIS
    Bounded CPU stress worker for PROMETHEUS benchmarks.

.DESCRIPTION
    Saturates N logical CPU cores for a capped duration using pure PowerShell
    arithmetic — no native executables, no downloads, no network activity.

    Safety guarantees:
      - Hard duration cap: exits after MaxDurationSec regardless of anything else.
      - Kill file: creates $KillFile on start; delete that file to request clean stop.
      - Never uses more than NumCores logical cores.
      - Each worker loop includes a minimal sleep to prevent 100% scheduler lockout.

.PARAMETER NumCores
    Number of logical CPU cores to stress (default 1).

.PARAMETER MaxDurationSec
    Hard upper limit in seconds (default 300 = 5 minutes).

.PARAMETER KillFile
    Path to the sentinel file. Delete this file to stop stress early.

.EXAMPLE
    # Stress 4 cores for up to 60 seconds
    .\stress-worker.ps1 -NumCores 4 -MaxDurationSec 60
#>
param(
    [ValidateRange(1,64)]
    [int]$NumCores = 1,

    [ValidateRange(1,600)]
    [int]$MaxDurationSec = 300,

    [string]$KillFile = "$env:TEMP\prometheus_stress.kill"
)

Set-StrictMode -Version Latest

# Create the kill-file sentinel
$null = New-Item -Path $KillFile -ItemType File -Force

$deadline = (Get-Date).AddSeconds($MaxDurationSec)

# Launch one background job per core
$jobs = 1..$NumCores | ForEach-Object {
    Start-Job -ScriptBlock {
        param($deadline, $killFile)
        $x = 1.0
        while ((Get-Date) -lt $deadline -and (Test-Path $killFile)) {
            # Pure floating-point burn — cannot access hardware or network
            for ($i = 0; $i -lt 100000; $i++) {
                $x = [math]::Sqrt($x + 1.0000001) * 1.0000001
            }
            Start-Sleep -Milliseconds 1   # yield once per 100k ops
        }
    } -ArgumentList $deadline, $KillFile
}

Write-Host "Stressing $NumCores core(s) for up to ${MaxDurationSec}s. Delete '$KillFile' to stop early."

# Wait for all jobs to complete or deadline
$null = Wait-Job $jobs -Timeout ($MaxDurationSec + 5)

# Clean up
Stop-Job $jobs -ErrorAction SilentlyContinue
Remove-Job $jobs -Force -ErrorAction SilentlyContinue
if (Test-Path $KillFile) { Remove-Item $KillFile -Force }

Write-Host "Stress complete."
