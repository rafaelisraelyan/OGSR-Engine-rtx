# Round 4 status-contract self-tests for both verifier runners.
# Every case starts with a real-format baseline response, then returns a
# controlled verifier response.  Only unique, non-empty statuses may pass.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'verifier-harness-common.ps1')

$utf8 = New-Object System.Text.UTF8Encoding($false)
$root = Join-Path $env:TEMP ('b3b2b-r4-status-' + [guid]::NewGuid().ToString('N'))
$source = Join-Path $root 'source'
$stubDir = Join-Path $root 'stubs'
New-Item -ItemType Directory -Force -Path $source, $stubDir | Out-Null
$manifestPath = Join-Path $source 'run-manifest.txt'
$manifest = "run_id=selftest`noutput_base=$root`nrun_directory=$source`n"
[IO.File]::WriteAllText($manifestPath, $manifest, $utf8)
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
$summary = "run_id=selftest`nrun_directory=$source`nrun_manifest_sha256=$manifestHash`n" +
           "frames_sampled=2`n"
[IO.File]::WriteAllText((Join-Path $source 'summary.txt'), $summary, $utf8)
New-Item -ItemType Directory -Force -Path (Join-Path $source 'sampled') | Out-Null
[IO.File]::WriteAllText((Join-Path $source 'sampled\evidence-manifest.txt'),
    "run_id=selftest`nframe=1 color_sha256=$('a' * 64) depth_sha256=$('b' * 64) meta_sha256=$('c' * 64)`n" +
    "frame=2 color_sha256=$('a' * 64) depth_sha256=$('b' * 64) meta_sha256=$('c' * 64)`n", $utf8)
[IO.File]::WriteAllText((Join-Path $source 'gpu-validation.txt'),
    "validation_status=PASS`ninfoqueue_error=0`n", $utf8)
[IO.File]::WriteAllText((Join-Path $source 'gpu-log.txt'),
    "fence stage=geometry upload expected=1 completed=1 confirmed=1`n", $utf8)
[IO.File]::WriteAllText((Join-Path $source 'persistent-counters.txt'),
    "frames_completed=2`nframes_submitted=2`nframes_sampled=2`nframes_received=2`n", $utf8)
[IO.File]::WriteAllText((Join-Path $source 'frames.csv'),
    "run_id,frame_index,kind`nselftest,1,offline`nselftest,2,offline`n", $utf8)

function New-Stub([string]$name, [string[]]$negativeLines, [int]$negativeExit) {
    $path = Join-Path $stubDir ($name + '.cmd')
    $lines = (($negativeLines | ForEach-Object { "echo $_" }) -join "`r`n")
    $text = @"
@echo off
setlocal EnableExtensions
set "KEY=%B3B2B_SELFTEST_TOKEN%-%~2"
set "KEY=%KEY:\=_%"
set "KEY=%KEY::=_%"
set "KEY=%KEY:/=_%"
set "KEY=%KEY: =_%"
set "ROOT=%TEMP%\b3b2b-r4-status"
if not exist "%ROOT%" mkdir "%ROOT%"
set "F=%ROOT%\%KEY%.flag"
if exist "%F%" goto negative
echo 1>"%F%"
echo evidence_status=PASS
echo acceptance_status=PASS
exit /b 0
:negative
$lines
exit /b $negativeExit
"@
    [IO.File]::WriteAllText($path, ($text -replace "`r`n", "`n"), $utf8)
    return $path
}

$pass = New-Stub 'pass' @('evidence_status=FAIL', 'acceptance_status=UNVERIFIED') 3
$validNegative = New-Stub 'valid-negative' @('acceptance_status=UNVERIFIED', 'evidence_status=FAIL') 3
$dupEvidence = New-Stub 'duplicate-evidence' @(
    'evidence_status=FAIL', 'acceptance_status=UNVERIFIED',
    'evidence_status=PASS', 'acceptance_status=PASS') 3
$dupAcceptance = New-Stub 'duplicate-acceptance' @(
    'evidence_status=FAIL', 'acceptance_status=UNVERIFIED',
    'acceptance_status=PASS') 3
$dupSame = New-Stub 'duplicate-same' @(
    'evidence_status=FAIL', 'evidence_status=FAIL', 'acceptance_status=UNVERIFIED') 3
$dupSameAcceptance = New-Stub 'duplicate-same-acceptance' @(
    'evidence_status=FAIL', 'acceptance_status=UNVERIFIED',
    'acceptance_status=UNVERIFIED') 3
$emptyStatus = New-Stub 'empty-status' @('evidence_status=', 'acceptance_status=UNVERIFIED') 3
$unknownStatus = New-Stub 'unknown-status' @('evidence_status=UNKNOWN', 'acceptance_status=UNVERIFIED') 3
$wrongExit = New-Stub 'wrong-exit' @('evidence_status=FAIL', 'acceptance_status=UNVERIFIED') 0
$exit3Pass = New-Stub 'exit3-pass-unverified' @(
    'evidence_status=PASS', 'acceptance_status=UNVERIFIED') 3
$exit1Pass = New-Stub 'exit1-pass-pass' @(
    'evidence_status=PASS', 'acceptance_status=PASS') 1
$missingEvidence = New-Stub 'missing-evidence' @('acceptance_status=UNVERIFIED') 3
$missingAcceptance = New-Stub 'missing-acceptance' @('evidence_status=FAIL') 3

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
    if (-not $condition) { throw "R4 STATUS SELFTEST FAILED: $message" }
}

$p1p4 = Join-Path $PSScriptRoot 'run-verifier-p1p4.ps1'
$faults = Join-Path $PSScriptRoot 'run-verifier-faults.ps1'
$caseIndex = 0

function Run-P1P4([string]$stub, [bool]$expectSuccess, [string]$label) {
    $script:caseIndex++
    $outRoot = Join-Path $root ("p1p4-" + $script:caseIndex)
    $env:B3B2B_SELFTEST_TOKEN = [guid]::NewGuid().ToString('N')
    $result = Invoke-Runner $p1p4 @('-Verifier', $stub, '-SourceRun', $source, '-OutputRoot', $outRoot)
    if ($expectSuccess) {
        Assert ($result.Exit -eq 0) "P1P4 $label expected 0, got $($result.Exit)"
    } else {
        Assert ($result.Exit -ne 0) "P1P4 $label expected non-zero, got 0"
    }
}

function Run-Faults([string]$stub, [bool]$expectSuccess, [string]$label) {
    $script:caseIndex++
    $outRoot = Join-Path $root ("faults-" + $script:caseIndex)
    $env:B3B2B_SELFTEST_TOKEN = [guid]::NewGuid().ToString('N')
    $result = Invoke-Runner $faults @('-NormalExe', $stub, '-FaultExe', $stub, '-BaselineRun', $source, '-OutDir', $outRoot)
    if ($expectSuccess) {
        Assert ($result.Exit -eq 0) "Faults $label expected 0, got $($result.Exit)"
    } else {
        Assert ($result.Exit -ne 0) "Faults $label expected non-zero, got 0"
    }
}

$cases = @(
    @{ Stub = $pass; Success = $true; Label = 'exit0 PASS/PASS CRLF' },
    @{ Stub = $validNegative; Success = $true; Label = 'exit3 FAIL/UNVERIFIED reversed order CRLF' },
    @{ Stub = $exit3Pass; Success = $false; Label = 'exit3 PASS/UNVERIFIED' },
    @{ Stub = $wrongExit; Success = $false; Label = 'exit0 FAIL/UNVERIFIED' },
    @{ Stub = $dupEvidence; Success = $false; Label = 'duplicate evidence conflict' },
    @{ Stub = $dupAcceptance; Success = $false; Label = 'duplicate acceptance conflict' },
    @{ Stub = $dupSame; Success = $false; Label = 'duplicate identical evidence' },
    @{ Stub = $dupSameAcceptance; Success = $false; Label = 'duplicate identical acceptance' },
    @{ Stub = $missingEvidence; Success = $false; Label = 'missing evidence status' },
    @{ Stub = $missingAcceptance; Success = $false; Label = 'missing acceptance status' },
    @{ Stub = $emptyStatus; Success = $false; Label = 'empty evidence status' },
    @{ Stub = $unknownStatus; Success = $false; Label = 'unknown evidence status' },
    @{ Stub = $exit1Pass; Success = $false; Label = 'invalid exit1 PASS/PASS' }
)
foreach ($case in $cases) {
    Run-P1P4 $case.Stub $case.Success $case.Label
    Run-Faults $case.Stub $case.Success $case.Label
}
Write-Host "verifier_round4_status_selftest=PASS cases=$($caseIndex)"
exit 0
