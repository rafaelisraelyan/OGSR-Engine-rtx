param(
    [Parameter(Mandatory=$true)][string]$NormalExe,
    [Parameter(Mandatory=$true)][string]$FaultExe,
    [Parameter(Mandatory=$true)][string]$BaselineRun,
    [Parameter(Mandatory=$true)][string]$OutDir
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'verifier-harness-common.ps1')
$utf8 = New-Object System.Text.UTF8Encoding($false)
$source = (Resolve-Path -LiteralPath $BaselineRun).Path
$normal = (Resolve-Path -LiteralPath $NormalExe).Path
$fault = (Resolve-Path -LiteralPath $FaultExe).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$root = (Resolve-Path -LiteralPath $OutDir).Path
if ($root.Equals($source, [StringComparison]::OrdinalIgnoreCase) -or
    $root.StartsWith($source + [IO.Path]::DirectorySeparatorChar,
                     [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutDir must not be BaselineRun or its descendant'
}
$rows = @('case,baseline_exit,mutated_exit,expected,evidence_status,acceptance_status,verdict')
function ReadUtf8([string]$p) { return [IO.File]::ReadAllText($p, [Text.Encoding]::UTF8) }
function WriteUtf8([string]$p, [string]$t) { [IO.File]::WriteAllText($p, $t, $utf8) }
function Relocate([string]$dir) {
    $mp = Join-Path $dir 'run-manifest.txt'
    $sp = Join-Path $dir 'summary.txt'
    $m = ReadUtf8 $mp
    $m = [regex]::Replace($m, '(?m)^run_directory=[^\r\n]*', 'run_directory='+$dir)
    $m = [regex]::Replace($m, '(?m)^output_base=[^\r\n]*', 'output_base='+$root)
    WriteUtf8 $mp $m
    $h = (Get-FileHash -LiteralPath $mp -Algorithm SHA256).Hash.ToUpperInvariant()
    $s = ReadUtf8 $sp
    $s = [regex]::Replace($s, '(?m)^run_directory=[^\r\n]*', 'run_directory='+$dir)
    $s = [regex]::Replace($s, '(?m)^run_manifest_sha256=[^\r\n]*', 'run_manifest_sha256='+$h)
    WriteUtf8 $sp $s
}
function RunVerifier([string]$exe, [string]$dir, [string]$log) {
    return Invoke-VerifierProcess -Exe $exe -Directory $dir -LogPath $log
}
function CopyBaseline([string]$name) {
    $dir = Join-Path $root $name
    if (Test-Path -LiteralPath $dir) { throw "Existing case directory: $dir" }
    Copy-Item -LiteralPath $source -Destination $dir -Recurse -Force
    Relocate $dir
    return $dir
}
function AddResult([string]$name, $baseline, $mutated, [int]$expected, [bool]$valid) {
    $ok = if ($valid) {
        $mutated.Exit -eq $expected -and (Test-BaselineResult $mutated)
    } else {
        $mutated.Exit -eq $expected -and (Test-NegativeResult $mutated)
    }
    $script:rows += "$name,$($baseline.Exit),$($mutated.Exit),$expected,$($mutated.Evidence),$($mutated.Acceptance),$(if($ok){'PASS'}else{'FAIL'})"
    if (-not $ok) { throw "case failed: $name" }
}
$normalResult = RunVerifier $normal $source (Join-Path $root 'normal.log')
AddResult 'normal' ([pscustomobject]@{Exit=0}) $normalResult 0 $true

foreach ($name in @('locked_gpu_log','locked_validation','empty_log','missing_log',
                     'truncated_log','no_marker','csv_truncated',
                     'summary_run_id_mismatch','locked_csv')) {
    $dir = CopyBaseline $name
    $before = RunVerifier $normal $dir (Join-Path $dir '_baseline.log')
    if (-not (Test-BaselineResult $before)) {
        throw "baseline failed for $name exit=$($before.Exit)"
    }
    $lock = $null
    switch ($name) {
        'locked_gpu_log' {
            $lock = [IO.File]::Open((Join-Path $dir 'gpu-log.txt'), [IO.FileMode]::Open,
                [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        }
        'locked_validation' {
            $lock = [IO.File]::Open((Join-Path $dir 'gpu-validation.txt'), [IO.FileMode]::Open,
                [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        }
        'empty_log' { WriteUtf8 (Join-Path $dir 'gpu-log.txt') '' }
        'missing_log' { Remove-Item (Join-Path $dir 'gpu-log.txt') -Force }
        'truncated_log' {
            $p = Join-Path $dir 'gpu-log.txt'; $b = [IO.File]::ReadAllBytes($p)
            [IO.File]::WriteAllBytes($p, $b[0..([Math]::Min(50, $b.Length - 1))])
        }
        'no_marker' { WriteUtf8 (Join-Path $dir 'gpu-log.txt') 'no_required_marker' }
        'csv_truncated' {
            $p = Join-Path $dir 'frames.csv'; $b = [IO.File]::ReadAllBytes($p)
            [IO.File]::WriteAllBytes($p, $b[0..([Math]::Min(120, $b.Length - 1))])
        }
        'summary_run_id_mismatch' {
            $s = ReadUtf8 (Join-Path $dir 'summary.txt')
            $s = [regex]::Replace($s, '(?m)^run_id=[^\r\n]*', 'run_id=stale-run-id')
            WriteUtf8 (Join-Path $dir 'summary.txt') $s
        }
        'locked_csv' {
            $lock = [IO.File]::Open((Join-Path $dir 'frames.csv'), [IO.FileMode]::Open,
                [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        }
    }
    try {
        $after = RunVerifier $normal $dir (Join-Path $dir '_mutated.log')
    } finally {
        if ($lock) { $lock.Close() }
    }
    AddResult $name $before $after 3 $false
}

foreach ($name in @('partial_read','stream_badbit')) {
    $dir = CopyBaseline $name
    Remove-Item Env:\B3B2B_READ_FAULT -ErrorAction SilentlyContinue
    Remove-Item Env:\B3B2B_READ_FAULT_TARGET -ErrorAction SilentlyContinue
    $before = RunVerifier $fault $dir (Join-Path $dir '_baseline.log')
    if (-not (Test-BaselineResult $before)) {
        throw "fault-exe baseline failed for $name exit=$($before.Exit)"
    }
    $env:B3B2B_READ_FAULT = if ($name -eq 'partial_read') { 'partial' } else { 'badbit' }
    $env:B3B2B_READ_FAULT_TARGET = 'gpu-log.txt'
    try { $after = RunVerifier $fault $dir (Join-Path $dir '_mutated.log') }
    finally {
        Remove-Item Env:\B3B2B_READ_FAULT -ErrorAction SilentlyContinue
        Remove-Item Env:\B3B2B_READ_FAULT_TARGET -ErrorAction SilentlyContinue
    }
    AddResult $name $before $after 3 $false
}
$rows | Set-Content (Join-Path $root 'verifier-results.csv') -Encoding ascii
$rows | ForEach-Object { Write-Host $_ }
if ($rows | Select-String ',FAIL$') { exit 1 }
exit 0
