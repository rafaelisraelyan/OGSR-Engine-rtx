# Stub-based self-tests for both verifier harnesses.
# They exercise the shared status contract used by run-verifier-p1p4.ps1 and
# run-verifier-faults.ps1 without running a real GPU/evidence verifier.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'verifier-harness-common.ps1')

$utf8 = New-Object System.Text.UTF8Encoding($false)
$root = Join-Path $env:TEMP ('b3b2b-harness-selftest-' + [guid]::NewGuid().ToString('N'))
$source = Join-Path $root 'source'
$stubDir = Join-Path $root 'stubs'
New-Item -ItemType Directory -Force -Path $source, $stubDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $source 'sampled') | Out-Null

function Write-Text([string]$path, [string]$text) {
    [System.IO.File]::WriteAllText($path, $text, $utf8)
}

$manifestPath = Join-Path $source 'run-manifest.txt'
$manifest = "run_id=selftest" + "`n" +
            "pid=1" + "`n" +
            "started_utc=20260101T000000Z" + "`n" +
            "output_base=$root" + "`n" +
            "run_directory=$source" + "`n" +
            "mode=offline" + "`n" +
            "sample_every=1" + "`n"
Write-Text $manifestPath $manifest
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
$summary = "run_id=selftest" + "`n" +
           "run_directory=$source" + "`n" +
           "run_manifest_sha256=$manifestHash" + "`n" +
           "mode=offline" + "`n" +
           "sample_every=1" + "`n" +
           "acceptance_status=PASS" + "`n" +
           "evidence_status=PASS" + "`n" +
           "gpu_status=PASS" + "`n" +
           "validation_status=PASS" + "`n" +
           "validation_passed=1" + "`n" +
           "visibility_status=PASS" + "`n" +
           "status=PASS" + "`n" +
           "evidence_failures=0" + "`n" +
           "render_failures=0" + "`n" +
           "frames_completed=2" + "`n" +
           "frames_submitted=2" + "`n" +
           "frames_sampled=1" + "`n" +
           "frames_received=2" + "`n" +
           "sampled_visible_frames=1" + "`n"
Write-Text (Join-Path $source 'summary.txt') $summary

$columns = 'run_id,frame_index,kind,connection_generation,level_session,capture_epoch,capture_sequence,source_game_frame,camera_hash,camera_sha256,viewport,position,frame_received_qpc,gpu_submit_qpc,gpu_completion_qpc,frame_age_ms,camera_decode_us,draw_record_us,gpu_wait_us,fence_value,sample,coverage,non_clear_pixels,bbox,depth_min,depth_max,status'
$row1 = 'selftest,1,offline,1,1,1,1,1,0000000000000001,' + ('a' * 64) + ',1280x720,"(1,2,3)",10,20,30,0,1,1,1,2,1,VISIBLE_DRAW_VERIFIED,1,"0,0..0,0",0.5,1,VISIBLE_DRAW_VERIFIED'
$row2 = 'selftest,2,offline,1,1,1,1,2,0000000000000002,' + ('b' * 64) + ',1280x720,"(1,2,3)",10,20,30,0,1,1,1,3,0,GPU_COMPLETED_NOT_READBACK_VERIFIED,0,"0,0..0,0",1,1,GPU_COMPLETED_NOT_READBACK_VERIFIED'
Write-Text (Join-Path $source 'frames.csv') ($columns + "`n" + $row1 + "`n" + $row2 + "`n")
Write-Text (Join-Path $source 'sampled\evidence-manifest.txt') ("run_id=selftest`n" +
    "frame=1 color_sha256=" + ('a' * 64) + " depth_sha256=" + ('b' * 64) + " meta_sha256=" + ('c' * 64) + "`n" +
    "frame=2 color_sha256=" + ('a' * 64) + " depth_sha256=" + ('b' * 64) + " meta_sha256=" + ('c' * 64) + "`n")
Write-Text (Join-Path $source 'gpu-validation.txt') ("validation_status=PASS`n" +
    "infoqueue_error=0`n" +
    "infoqueue_warning=0`n" +
    "completion_confirmed=1`n")
Write-Text (Join-Path $source 'gpu-log.txt') ("fence stage=geometry upload expected=1 completed=1 signal=1 arm=1 wait=1 removed=0x00000000 outcome=Completed confirmed=1`n" +
    "fence stage=camera frame expected=2 completed=2 signal=1 arm=1 wait=1 removed=0x00000000 outcome=Completed confirmed=1`n" +
    "fence stage=camera frame expected=3 completed=3 signal=1 arm=1 wait=1 removed=0x00000000 outcome=Completed confirmed=1`n")
Write-Text (Join-Path $source 'persistent-counters.txt') ("run_id=selftest`n" +
    "frames_completed=2`n" +
    "frames_submitted=2`n" +
    "frames_sampled=1`n" +
    "frames_received=2`n")
Write-Text (Join-Path $source 'geometry-audit.txt') "visual_id=6470`n"
Write-Text (Join-Path $source 'readback-audit.txt') "frame_index=1`n"
Write-Text (Join-Path $source 'camera-audit.txt') "camera_hash=0000000000000001`n"
Write-Text (Join-Path $source 'infoqueue-dump.txt') "index=0 severity=3 id=1 description_length=1 description=x`n"

function Write-Stub([string]$name, [string]$negativeEvidence, [string]$negativeAcceptance, [int]$negativeExit) {
    $path = Join-Path $stubDir ($name + '.cmd')
    $text = @"
@echo off
setlocal EnableExtensions
set "KEY=%B3B2B_SELFTEST_TOKEN%-%~2"
set "KEY=%KEY:\=_%"
set "KEY=%KEY::=_%"
set "KEY=%KEY:/=_%"
set "KEY=%KEY: =_%"
set "ROOT=%TEMP%\b3b2b-harness-selftest"
if not exist "%ROOT%" mkdir "%ROOT%"
set "F=%ROOT%\%KEY%.flag"
if exist "%F%" goto negative
echo 1>"%F%"
echo evidence_status=PASS
echo acceptance_status=PASS
exit /b 0
:negative
echo evidence_status=$negativeEvidence
echo acceptance_status=$negativeAcceptance
exit /b $negativeExit
"@
    Write-Text $path ($text -replace "`r`n", "`n")
    return $path
}

$pass = Write-Stub 'pass' 'FAIL' 'UNVERIFIED' 3
$contradictory = Write-Stub 'contradictory' 'PASS' 'UNVERIFIED' 3
$missing = Write-Stub 'missing' '' '' 3
$badExit = Write-Stub 'bad-exit' 'PASS' 'PASS' 3

function Invoke-Runner([string]$script, [string[]]$arguments) {
    $quoted = for ($i = 0; $i -lt $arguments.Count; ++$i) {
        if ($arguments[$i].StartsWith('-')) { $arguments[$i] }
        else { '"' + $arguments[$i].Replace('"', '\"') + '"' }
    }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'powershell.exe'
    $psi.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $script + '" ' + ($quoted -join ' ')
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::Start($psi)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    [pscustomobject]@{ Exit = [int]$process.ExitCode; Output = $stdout + $stderr }
}

function Assert([bool]$condition, [string]$message) {
    if (-not $condition) { throw "SELFTEST FAILED: $message" }
}

$p1p4 = Join-Path $PSScriptRoot 'run-verifier-p1p4.ps1'
$faults = Join-Path $PSScriptRoot 'run-verifier-faults.ps1'
$caseIndex = 0

function Run-P1P4([string]$stub, [bool]$expectPass) {
    $script:caseIndex++
    $outRoot = Join-Path $root ("p1p4-" + $script:caseIndex)
    $env:B3B2B_SELFTEST_TOKEN = [guid]::NewGuid().ToString('N')
    $result = Invoke-Runner $p1p4 @('-Verifier', $stub, '-SourceRun', $source, '-OutputRoot', $outRoot)
    if ($expectPass) {
        Assert ($result.Exit -eq 0) "P1P4 expected exit 0, got $($result.Exit): $($result.Output)"
    } else {
        Assert ($result.Exit -ne 0) "P1P4 expected non-zero for contradictory/missing statuses"
    }
}

function Run-Faults([string]$stub, [bool]$expectPass) {
    $script:caseIndex++
    $outRoot = Join-Path $root ("faults-" + $script:caseIndex)
    $env:B3B2B_SELFTEST_TOKEN = [guid]::NewGuid().ToString('N')
    $result = Invoke-Runner $faults @('-NormalExe', $stub, '-FaultExe', $stub, '-BaselineRun', $source, '-OutDir', $outRoot)
    if ($expectPass) {
        Assert ($result.Exit -eq 0) "Fault harness expected exit 0, got $($result.Exit): $($result.Output)"
    } else {
        Assert ($result.Exit -ne 0) "Fault harness expected non-zero for contradictory/missing statuses"
    }
}

Run-P1P4 $pass $true
Run-P1P4 $contradictory $false
Run-P1P4 $missing $false
Run-P1P4 $badExit $false
Run-Faults $pass $true
Run-Faults $contradictory $false
Run-Faults $missing $false

Write-Host 'verifier_harness_selftest=PASS cases=7'
exit 0
