# F4.0-B3-B2B P4: Every negative case is independently baseline PASS (exit 0)
# BEFORE exactly one mutation. Never modify -SourceRun or original evidence.
param(
    [Parameter(Mandatory=$true)][string]$Verifier,
    [Parameter(Mandatory=$true)][string]$SourceRun,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'verifier-harness-common.ps1')
$utf8 = New-Object System.Text.UTF8Encoding($false)
$source = (Resolve-Path -LiteralPath $SourceRun).Path
$exe = (Resolve-Path -LiteralPath $Verifier).Path
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$root = (Resolve-Path -LiteralPath $OutputRoot).Path
if ($root.Equals($source, [System.StringComparison]::OrdinalIgnoreCase) -or
    $root.StartsWith($source + [System.IO.Path]::DirectorySeparatorChar,
                     [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputRoot must not be SourceRun or a descendant of SourceRun'
}
$cases = @('manifest_none', 'manifest_last', 'manifest_duplicate', 'validation_fail',
           'infoqueue_error', 'fence_unconfirmed', 'counters_zero',
           'csv_truncated', 'csv_duplicate', 'summary_samples_99')
function ReadUtf8([string]$path) { return [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8) }
function WriteUtf8([string]$path, [string]$content) { [System.IO.File]::WriteAllText($path, $content, $utf8) }
function ReplaceOnce([string]$path, [string]$before, [string]$after) {
    $text = ReadUtf8 $path
    $pos = $text.IndexOf($before, [System.StringComparison]::Ordinal)
    if ($pos -lt 0) { throw "Mutation token missing: $before in $path" }
    WriteUtf8 $path ($text.Substring(0,$pos) + $after + $text.Substring($pos+$before.Length))
}
function RunCheck([string]$dir, [string]$logPath) {
    return Invoke-VerifierProcess -Exe $exe -Directory $dir -LogPath $logPath
}
$results = @()
foreach ($case in $cases) {
    $dir = Join-Path $root ("case-"+$case)
    if (Test-Path -LiteralPath $dir) { throw "Existing case directory: $dir (no overwrites)" }
    Copy-Item -LiteralPath $source -Destination $dir -Recurse -Force
    $manifestPath = Join-Path $dir 'run-manifest.txt'
    $summaryPath = Join-Path $dir 'summary.txt'
    # Manifest holds absolute run_directory; move is not valid evidence until
    # run_directory/output_base/summary manifest SHA are recomputed.
    $manifest = ReadUtf8 $manifestPath
    $manifest = [regex]::Replace($manifest, '(?m)^run_directory=[^\r\n]*', 'run_directory='+$dir)
    $manifest = [regex]::Replace($manifest, '(?m)^output_base=[^\r\n]*', 'output_base='+$root)
    WriteUtf8 $manifestPath $manifest
    # Producer/verifier use uppercase SHA-256 hex for run_manifest_sha256.
    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    $summary = ReadUtf8 $summaryPath
    $summary = [regex]::Replace($summary, '(?m)^run_directory=[^\r\n]*', 'run_directory='+$dir)
    $summary = [regex]::Replace($summary, '(?m)^run_manifest_sha256=[^\r\n]*', 'run_manifest_sha256='+$manifestHash)
    WriteUtf8 $summaryPath $summary

    $before = RunCheck $dir (Join-Path $dir '_baseline-before.log')
    if (-not (Test-BaselineResult $before)) {
        throw "Invalid relocated baseline for $case, exit=$($before.Exit), evidence=$($before.Evidence), acceptance=$($before.Acceptance). Mutation NOT applied."
    }

    $sampleManifest = Join-Path $dir 'sampled\evidence-manifest.txt'
    $gpuValidation = Join-Path $dir 'gpu-validation.txt'
    $gpuLog = Join-Path $dir 'gpu-log.txt'
    $counters = Join-Path $dir 'persistent-counters.txt'
    $csv = Join-Path $dir 'frames.csv'
    switch ($case) {
        'manifest_none' {
            $s = ReadUtf8 $sampleManifest
            WriteUtf8 $sampleManifest ([regex]::Replace($s,'(?m)^frame=[^\r\n]*(\r?\n)?',''))
        }
        'manifest_last' {
            $s = ReadUtf8 $sampleManifest
            $matches = [regex]::Matches($s,'(?m)^frame=[^\r\n]*(\r?\n)?')
            if ($matches.Count -eq 0) { throw 'no sample entries' }
            $m = $matches[$matches.Count-1]
            WriteUtf8 $sampleManifest ($s.Remove($m.Index,$m.Length))
        }
        'manifest_duplicate' {
            $s = ReadUtf8 $sampleManifest
            $m = [regex]::Match($s,'(?m)^frame=[^\r\n]*(\r?\n)?')
            if (-not $m.Success) { throw 'no sample entries' }
            WriteUtf8 $sampleManifest ($s + $m.Value)
        }
        'validation_fail' { ReplaceOnce $gpuValidation 'validation_status=PASS' 'validation_status=FAIL' }
        'infoqueue_error' { ReplaceOnce $gpuValidation 'infoqueue_error=0' 'infoqueue_error=1' }
        'fence_unconfirmed' { ReplaceOnce $gpuLog 'confirmed=1' 'confirmed=0' }
        'counters_zero' { $s = ReadUtf8 $counters; $s = [regex]::Replace($s,'(?m)^frames_completed=\d+','frames_completed=0'); WriteUtf8 $counters $s }
        'csv_truncated' {
            $lines = @( (ReadUtf8 $csv) -split '\r?\n' | Where-Object { $_.Length -gt 0 } )
            if ($lines.Count -lt 2) { throw 'missing data rows' }
            $lines[$lines.Count-1] = ($lines[$lines.Count-1] -split ',')[0] + ','
            WriteUtf8 $csv (($lines -join "`n")+"`n")
        }
        'csv_duplicate' {
            $lines = @( (ReadUtf8 $csv) -split '\r?\n' | Where-Object { $_.Length -gt 0 } )
            if ($lines.Count -lt 3) { throw 'need at least 2 frames for duplicate mutation' }
            $parts = $lines[2] -split ',',3
            $first = $lines[1] -split ',',3
            $lines[2] = $parts[0] + ',' + $first[1] + ',' + $parts[2]
            WriteUtf8 $csv (($lines -join "`n")+"`n")
        }
        'summary_samples_99' {
            $s = ReadUtf8 $summaryPath
            $s = [regex]::Replace($s,'(?m)^frames_sampled=\d+','frames_sampled=99')
            WriteUtf8 $summaryPath $s
        }
    }
    $after = RunCheck $dir (Join-Path $dir '_mutated-after.log')
    $pass = Test-NegativeResult $after
    $results += [pscustomobject]@{Case=$case;BaselineExit=$before.Exit;MutatedExit=$after.Exit;Evidence=$after.Evidence;Acceptance=$after.Acceptance;Pass=$pass;RunDirectory=$dir}
    Write-Host "$case baseline=$($before.Exit) mutation=$($after.Exit) evidence=$($after.Evidence) acceptance=$($after.Acceptance) pass=$pass"
}
$results | Export-Csv -NoTypeInformation -Encoding UTF8 -Path (Join-Path $root 'p1p4-probe-results.csv')
if (@($results | Where-Object { -not $_.Pass }).Count -gt 0) { exit 1 }
exit 0
