# F3.5.4B - reproducible scaling benchmark for DX12 parallel command recording.
#
# Design constraints:
#   - one process per configuration (no state carried between runs);
#   - no CSV/JSON I/O inside the measured frame window: the smoke target keeps
#     per-frame samples in memory and writes one CSV after Shutdown;
#   - correctness first (Debug Layer + GBV), performance second (-nodebug -nogbv);
#   - speedup is computed against a legacy baseline with the same draw count,
#     resolution and batch conditions;
#   - the pilot plan is intentionally smaller than the full Cartesian product.
#
# The script is ASCII-only so Windows PowerShell 5.1 can parse it reliably.

[CmdletBinding()]
param(
    [string]$Root = "",
    [string]$Exe = "",
    [string]$OutDir = "",
    [int]$Runs = 5,
    [int]$WarmupFrames = 30,
    [int]$MeasuredFrames = 300,
    [int]$CorrectnessWorkers = 4,
    [double]$MaxIqrRatio = 0.15,
    [switch]$SkipCorrectness,
    [switch]$SkipPerformance
)

$ErrorActionPreference = "Stop"
# CSV/JSON numbers must use a decimal point regardless of the host locale.
# Otherwise Export-Csv writes "8,93" and a later Import-Csv/InvariantCulture
# read turns it into 893. All benchmark artifacts use invariant formatting.
[System.Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::InvariantCulture

function Write-Step([string]$Text) {
    Write-Host ("[bench] " + $Text)
}

function To-Double([string]$Text) {
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    return [double]::Parse($Text.Trim(), [Globalization.CultureInfo]::InvariantCulture)
}

function Get-Percentile {
    param([double[]]$Sorted, [double]$Percent)
    if ($null -eq $Sorted -or $Sorted.Count -eq 0) { return $null }
    if ($Sorted.Count -eq 1) { return [double]$Sorted[0] }
    $rank = ($Sorted.Count - 1) * $Percent / 100.0
    $lo = [int][math]::Floor($rank)
    $hi = [int][math]::Ceiling($rank)
    if ($lo -eq $hi) { return [double]$Sorted[$lo] }
    return [double]$Sorted[$lo] + ([double]$Sorted[$hi] - [double]$Sorted[$lo]) * ($rank - $lo)
}

function Get-Stats {
    param($Values)
    $clean = @($Values | Where-Object { $null -ne $_ -and -not [double]::IsNaN($_) })
    if ($clean.Count -eq 0) {
        return [pscustomobject]@{ Count = 0; Mean = $null; Min = $null; P5 = $null; P25 = $null; Median = $null; P75 = $null; P95 = $null; Max = $null; IQR = $null }
    }
    $sorted = @($clean | Sort-Object)
    $p25 = Get-Percentile -Sorted $sorted -Percent 25
    $p75 = Get-Percentile -Sorted $sorted -Percent 75
    $mean = ($clean | Measure-Object -Average).Average
    return [pscustomobject]@{
        Count  = $clean.Count
        Mean   = [double]$mean
        Min    = [double]$sorted[0]
        P5     = Get-Percentile -Sorted $sorted -Percent 5
        P25    = $p25
        Median = Get-Percentile -Sorted $sorted -Percent 50
        P75    = $p75
        P95    = Get-Percentile -Sorted $sorted -Percent 95
        Max    = [double]$sorted[$sorted.Count - 1]
        IQR    = [double]$p75 - [double]$p25
    }
}

function Get-Match {
    param([string]$Text, [string]$Pattern)
    $m = [regex]::Match($Text, $Pattern)
    if ($m.Success) { return $m.Groups[1].Value }
    return $null
}

function Get-GpuTelemetry {
    $smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if (-not $smi) { return $null }
    try {
        $line = & $smi.Source --query-gpu=temperature.gpu,clocks.sm,clocks.mem,power.draw --format=csv,noheader,nounits 2>$null | Select-Object -First 1
        if ([string]::IsNullOrWhiteSpace($line)) { return $null }
        $parts = $line -split "," | ForEach-Object { $_.Trim() }
        return [pscustomobject]@{
            TempC      = $parts[0]
            SmClockMHz = $parts[1]
            MemClockMHz = $parts[2]
            PowerW     = $parts[3]
        }
    } catch {
        return $null
    }
}

function New-Point {
    param([string]$Mode, [int]$Draws, [int]$Lists, [int]$Workers)
    $key = "$Mode|$Draws|$Lists|$Workers"
    return [pscustomobject]@{
        Key     = $key
        Mode    = $Mode
        Draws   = $Draws
        Lists   = $Lists
        Workers = $Workers
    }
}

function Add-UniquePoint {
    param($List, $Seen, [string]$Mode, [int]$Draws, [int]$Lists, [int]$Workers)
    $p = New-Point -Mode $Mode -Draws $Draws -Lists $Lists -Workers $Workers
    if (-not $Seen.ContainsKey($p.Key)) {
        $Seen[$p.Key] = $true
        [void]$List.Add($p)
    }
}

function Invoke-Smoke {
    param([string[]]$Arguments, [string]$MetricsPath, [string]$LogCopy)
    $argList = @()
    $argList += $Arguments
    if (-not [string]::IsNullOrWhiteSpace($MetricsPath)) {
        $argList += @("-metrics", ('"' + $MetricsPath + '"'))
    }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $Exe -ArgumentList $argList -WorkingDirectory $Root -WindowStyle Hidden -Wait -PassThru
    $sw.Stop()
    $logSource = Join-Path (Split-Path $Exe) "xrDX12Smoke.log"
    $text = ""
    if (Test-Path $logSource) {
        $text = Get-Content $logSource -Raw
        if (-not [string]::IsNullOrWhiteSpace($LogCopy)) {
            Copy-Item $logSource $LogCopy -Force
        }
    }
    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        WallMs   = $sw.Elapsed.TotalMilliseconds
        Log      = $text
    }
}

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
}
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $Root "x64\ReleaseDX12\xrDX12Smoke.exe"
}
if (-not (Test-Path $Exe)) {
    throw "Smoke executable not found: $Exe"
}
$Exe = (Resolve-Path $Exe).Path

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $Root ("x64\ReleaseDX12\benchmark\f354b-" + $stamp)
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$rawDir = Join-Path $OutDir "raw"
$logDir = Join-Path $OutDir "logs"
New-Item -ItemType Directory -Path $rawDir -Force | Out-Null
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

$totalFrames = $WarmupFrames + $MeasuredFrames
if ($totalFrames -gt 512) {
    throw "WarmupFrames + MeasuredFrames must be <= 512 (per-frame buffer limit)."
}
if ($Runs -lt 1) { throw "Runs must be >= 1." }
# CHW12 excludes the first 30 recording frames from aggregates and the
# per-frame sample buffer. A smaller warmup would silently produce no samples.
if ($WarmupFrames -lt 30) { throw "WarmupFrames must be >= 30 (renderer warmup contract)." }

$gitSha = ""
$gitDirty = ""
try { $gitSha = (& git -C $Root rev-parse HEAD 2>$null).Trim() } catch { $gitSha = "unknown" }
try { $gitDirty = ((& git -C $Root status --porcelain 2>$null) -join "`n") } catch { $gitDirty = "" }

$fingerprint = [pscustomobject]@{
    schema          = "f354b-1"
    createdAt       = (Get-Date).ToString("o")
    root            = $Root
    exe             = $Exe
    exeSha256       = (Get-FileHash $Exe -Algorithm SHA256).Hash
    scriptSha256    = (Get-FileHash $PSCommandPath -Algorithm SHA256).Hash
    gitSha          = $gitSha
    gitDirty        = (-not [string]::IsNullOrWhiteSpace($gitDirty))
    gitStatus       = $gitDirty
    os              = [System.Environment]::OSVersion.VersionString
    cpu             = (Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Name)
    logicalCpus     = [System.Environment]::ProcessorCount
    resolution      = "1280x720"
    adapter         = $null
    adapters        = @()
    requestedRuns   = $Runs
    warmupFrames    = $WarmupFrames
    measuredFrames  = $MeasuredFrames
    plan            = "pilot"
    ownerParticipates = $true
}

$gpus = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | Select-Object Name, DriverVersion, DriverDate, AdapterRAM, VideoProcessor)
$fingerprint.adapters = $gpus

# ---------------------------------------------------------------------------
# Correctness phase: Debug Layer + GBV, hash parity, exact draw count.
# ---------------------------------------------------------------------------
$correctness = New-Object System.Collections.Generic.List[object]
$legacyHash = @{}

if (-not $SkipCorrectness) {
    Write-Step "Correctness phase (Debug Layer + GBV)"

    $corrList = New-Object System.Collections.Generic.List[object]
    $corrSeen = @{}
    foreach ($d in @(128, 512, 2048)) {
        Add-UniquePoint -List $corrList -Seen $corrSeen -Mode "legacy" -Draws $d -Lists 1 -Workers 0
    }
    foreach ($m in @("multi", "parallel")) {
        foreach ($d in @(128, 512, 2048)) {
            foreach ($l in @(2, 4, 8)) {
                if ($l -le $d) {
                    $w = if ($m -eq "parallel") { $CorrectnessWorkers } else { 0 }
                    Add-UniquePoint -List $corrList -Seen $corrSeen -Mode $m -Draws $d -Lists $l -Workers $w
                }
            }
        }
    }

    foreach ($cp in $corrList) {
        $runId = "corr-{0}-d{1}-l{2}" -f $cp.Mode, $cp.Draws, $cp.Lists
        $logCopy = Join-Path $logDir ($runId + ".log")
        $args = @("-commandlists", $cp.Mode, "-depthtest", "-draws", "$($cp.Draws)", "-lists", "$($cp.Lists)")
        if ($cp.Mode -eq "parallel") {
            $args += @("-jobworkers", "$($cp.Workers)")
        }
        $res = Invoke-Smoke -Arguments $args -MetricsPath "" -LogCopy $logCopy
        $hash = Get-Match $res.Log '\[depth-frame\] hash=([0-9a-f]+)'
        $drawTotal = Get-Match $res.Log '\[recording-draws\] mode=\w+ lists=\d+ total=(\d+) expected=\d+'
        $drawExpected = Get-Match $res.Log '\[recording-draws\] mode=\w+ lists=\d+ total=\d+ expected=(\d+)'
        $drawLists = Get-Match $res.Log '\[recording-draws\] mode=\w+ lists=(\d+) total=\d+ expected=\d+'
        $valBad = ([regex]::Matches($res.Log, '\[validation\]\[(WARNING|ERROR|CORRUPTION)\]')).Count
        $debugOn = $res.Log -match '\[DX12\] Debug layer enabled'
        $gbvOn = $res.Log -match 'GPU-based validation=1'
        $resMatch = [regex]::Match($res.Log, 'Initialize complete: (\d+)x(\d+)')
        if ($resMatch.Success -and [string]::IsNullOrWhiteSpace($fingerprint.adapter)) {
            $fingerprint.resolution = "$($resMatch.Groups[1].Value)x$($resMatch.Groups[2].Value)"
        }
        $adapterName = Get-Match $res.Log '\[DX12\] GPU: (.+?) \(vendor:'
        if ($adapterName -and [string]::IsNullOrWhiteSpace($fingerprint.adapter)) {
            $fingerprint.adapter = $adapterName
        }

        if ($cp.Mode -eq "legacy") {
            if ($hash) { $legacyHash[$cp.Draws] = $hash }
        }
        $expectedHash = $null
        if ($legacyHash.ContainsKey($cp.Draws)) { $expectedHash = $legacyHash[$cp.Draws] }

        $pass = ($res.ExitCode -eq 0) -and
                ($valBad -eq 0) -and
                $debugOn -and $gbvOn -and
                ($drawTotal -eq "$($cp.Draws)") -and
                ($drawExpected -eq "$($cp.Draws)") -and
                ($drawLists -eq "$($cp.Lists)") -and
                ($hash -ne $null) -and
                ($expectedHash -ne $null) -and
                ($hash -eq $expectedHash)

        [void]$correctness.Add([pscustomobject]@{
            Mode          = $cp.Mode
            Draws         = $cp.Draws
            Lists         = $cp.Lists
            Workers       = $cp.Workers
            ExitCode      = $res.ExitCode
            Hash          = $hash
            LegacyHash    = $expectedHash
            DrawTotal     = $drawTotal
            DrawExpected  = $drawExpected
            DrawLists     = $drawLists
            ValidationBad = $valBad
            DebugLayer    = $debugOn
            GBV           = $gbvOn
            Pass          = $pass
            Log           = $logCopy
        })
        Write-Step ("correctness {0,-8} d={1,-4} l={2} exit={3} hash={4} bad={5} pass={6}" -f $cp.Mode, $cp.Draws, $cp.Lists, $res.ExitCode, $hash, $valBad, $pass)
    }

    $correctness | Export-Csv -Path (Join-Path $OutDir "correctness.csv") -NoTypeInformation -Encoding UTF8
    $failedCorrectness = @($correctness | Where-Object { -not $_.Pass })
    if ($failedCorrectness.Count -gt 0) {
        $fingerprint | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $OutDir "fingerprint.json") -Encoding UTF8
        throw "Correctness phase failed: $($failedCorrectness.Count) case(s). Performance phase not started."
    }
}

# Match adapter driver information to the adapter reported by the smoke log.
$matchedGpu = $null
if ($fingerprint.adapter) {
    $matchedGpu = $gpus | Where-Object {
        $_.Name -and (($fingerprint.adapter -like ("*" + $_.Name + "*")) -or ($_.Name -like ("*" + $fingerprint.adapter + "*")))
    } | Select-Object -First 1
}
if ($matchedGpu) {
    $fingerprint | Add-Member -NotePropertyName driverVersion -NotePropertyValue $matchedGpu.DriverVersion -Force
    $fingerprint | Add-Member -NotePropertyName driverDate -NotePropertyValue ([string]$matchedGpu.DriverDate) -Force
}
$fingerprint | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $OutDir "fingerprint.json") -Encoding UTF8

if ($SkipPerformance) {
    Write-Step "Performance phase skipped"
    return
}

# ---------------------------------------------------------------------------
# Pilot performance plan. This is deliberately not the full Cartesian product.
# ---------------------------------------------------------------------------
$points = New-Object System.Collections.Generic.List[object]
$seen = @{}
foreach ($d in @(128, 512, 2048)) {
    Add-UniquePoint -List $points -Seen $seen -Mode "legacy" -Draws $d -Lists 1 -Workers 0
}
foreach ($l in @(2, 4, 8)) {
    Add-UniquePoint -List $points -Seen $seen -Mode "multi" -Draws 2048 -Lists $l -Workers 0
}
Add-UniquePoint -List $points -Seen $seen -Mode "multi" -Draws 128 -Lists 8 -Workers 0
foreach ($d in @(128, 512, 2048)) {
    Add-UniquePoint -List $points -Seen $seen -Mode "parallel" -Draws $d -Lists 2 -Workers 1
    Add-UniquePoint -List $points -Seen $seen -Mode "parallel" -Draws $d -Lists 4 -Workers 2
    Add-UniquePoint -List $points -Seen $seen -Mode "parallel" -Draws $d -Lists 8 -Workers 4
}
Add-UniquePoint -List $points -Seen $seen -Mode "parallel" -Draws 2048 -Lists 4 -Workers 1
Add-UniquePoint -List $points -Seen $seen -Mode "parallel" -Draws 2048 -Lists 4 -Workers 4
Add-UniquePoint -List $points -Seen $seen -Mode "parallel" -Draws 2048 -Lists 2 -Workers 4
Add-UniquePoint -List $points -Seen $seen -Mode "parallel" -Draws 2048 -Lists 8 -Workers 4

$points = @($points | Sort-Object Mode, Draws, Lists, Workers)
Write-Step ("Pilot plan: {0} points x {1} runs = {2} processes; warmup={3}, measured={4}" -f $points.Count, $Runs, ($points.Count * $Runs), $WarmupFrames, $MeasuredFrames)

# ---------------------------------------------------------------------------
# Performance phase: -nodebug -nogbv, one process per run.
# ---------------------------------------------------------------------------
$runRows = New-Object System.Collections.Generic.List[object]
$allFrames = New-Object System.Collections.Generic.List[object]
$metrics = @("recording_ms", "scheduling_ms", "wait_ms", "submission_ms", "gpu_ms", "frame_interval_ms")

for ($runIndex = 0; $runIndex -lt $Runs; $runIndex++) {
    $offset = $runIndex % $points.Count
    for ($k = 0; $k -lt $points.Count; $k++) {
        $point = $points[($k + $offset) % $points.Count]
        $runId = "r{0:D2}-{1}-d{2}-l{3}-w{4}" -f $runIndex, $point.Mode, $point.Draws, $point.Lists, $point.Workers
        $rawPath = Join-Path $rawDir ($runId + ".csv")
        $logCopy = Join-Path $logDir ($runId + ".log")
        $telemetryBefore = Get-GpuTelemetry
        $startedAt = (Get-Date).ToString("o")

        $args = @(
            "-commandlists", $point.Mode,
            "-draws", "$($point.Draws)",
            "-lists", "$($point.Lists)",
            "-prepframes", "$totalFrames",
            "-nodebug", "-nogbv"
        )
        if ($point.Mode -eq "parallel") {
            $args += @("-jobworkers", "$($point.Workers)")
        }

        $res = Invoke-Smoke -Arguments $args -MetricsPath $rawPath -LogCopy $logCopy
        $endedAt = (Get-Date).ToString("o")
        $telemetryAfter = Get-GpuTelemetry

        $log = $res.Log
        $logMode = Get-Match $log '\[recording\] (?:PASS|ABORTED) mode=(\w+)'
        $logLists = Get-Match $log '\[recording\] (?:PASS|ABORTED) mode=\w+ lists=(\d+)'
        $logSamples = Get-Match $log '\[recording\] (?:PASS|ABORTED) mode=\w+ lists=\d+ frames=\d+ samples=(\d+)'
        $drawTotal = Get-Match $log '\[recording-draws\] mode=\w+ lists=\d+ total=(\d+) expected=\d+'
        $drawExpected = Get-Match $log '\[recording-draws\] mode=\w+ lists=\d+ total=\d+ expected=(\d+)'
        $drawLists = Get-Match $log '\[recording-draws\] mode=\w+ lists=(\d+) total=\d+ expected=\d+'
        $runResolutionMatch = [regex]::Match($log, 'Initialize complete: (\d+)x(\d+)')
        $runResolution = if ($runResolutionMatch.Success) {
            "$($runResolutionMatch.Groups[1].Value)x$($runResolutionMatch.Groups[2].Value)"
        } else { $null }
        $requestedDebug = Get-Match $log '\[smoke\] draw calls=\d+ draw lists=\d+ debug=(\d+) gbv=(\d+)'
        $actualWorkers = Get-Match $log '\[jobs\] ready: backgroundWorkers=(\d+) totalParticipants=(\d+)'
        $actualTotal = Get-Match $log '\[jobs\] ready: backgroundWorkers=\d+ totalParticipants=(\d+)'
        $valBad = ([regex]::Matches($log, '\[validation\]\[(WARNING|ERROR|CORRUPTION)\]')).Count
        $debugOff = $log -match '\[DX12\] Debug layer disabled by request'
        $gbvOff = $log -match '\[smoke\] .* debug=0 gbv=0'

        if ([string]::IsNullOrWhiteSpace($actualWorkers)) {
            $actualWorkers = "0"
            $actualTotal = "1"
        }
        if ($point.Mode -eq "legacy" -or $point.Mode -eq "multi") {
            $actualWorkers = "0"
            $actualTotal = "1"
        }

        $frameRows = @()
        if (Test-Path $rawPath) {
            $frameRows = @(Import-Csv $rawPath)
        }
        $valid = ($res.ExitCode -eq 0) -and
                 ($frameRows.Count -ge $MeasuredFrames) -and
                 ($logMode -eq $point.Mode) -and
                 ($drawTotal -eq "$($point.Draws)") -and
                 ($drawExpected -eq "$($point.Draws)") -and
                 ($drawLists -eq "$($point.Lists)") -and
                 ($runResolution -eq $fingerprint.resolution) -and
                 ($logSamples -ge $MeasuredFrames) -and
                 $debugOff -and $gbvOff -and
                 ($valBad -eq 0)
        if ($point.Mode -eq "parallel") {
            $valid = $valid -and ($actualWorkers -eq "$($point.Workers)")
        }

        $runStats = [ordered]@{}
        foreach ($metric in $metrics) {
            $values = @()
            foreach ($row in $frameRows) {
                $v = To-Double $row.$metric
                if ($null -ne $v -and $v -ge 0) { $values += $v }
            }
            $st = Get-Stats -Values $values
            foreach ($name in @("Count", "Mean", "Min", "P5", "P25", "Median", "P75", "P95", "Max", "IQR")) {
                $runStats[($metric + "_" + $name)] = $st.$name
            }
            foreach ($row in $frameRows) {
                $v = To-Double $row.$metric
                if ($null -eq $v -or $v -lt 0) { continue }
                [void]$allFrames.Add([pscustomobject]@{
                    RunId     = $runId
                    RunIndex  = $runIndex
                    Mode      = $point.Mode
                    Draws     = $point.Draws
                    Lists     = $point.Lists
                    Workers   = $point.Workers
                    Frame     = $row.frame
                    Metric    = $metric
                    Value     = $v
                })
            }
        }

        $runRow = [ordered]@{
            RunId             = $runId
            RunIndex          = $runIndex
            OrderIndex        = $k
            GlobalOrder       = ($runIndex * $points.Count + $k)
            Mode              = $point.Mode
            Draws             = $point.Draws
            Lists             = $point.Lists
            Resolution        = $runResolution
            RequestedWorkers  = $point.Workers
            ActualWorkers     = [int]$actualWorkers
            TotalParticipants = [int]$actualTotal
            OwnerThread       = $true
            ExitCode          = $res.ExitCode
            Valid             = $valid
            MeasuredFrames    = $frameRows.Count
            ExpectedFrames    = $MeasuredFrames
            WallMs            = [math]::Round($res.WallMs, 3)
            StartedAt         = $startedAt
            EndedAt           = $endedAt
            TempBeforeC       = $telemetryBefore.TempC
            TempAfterC        = $telemetryAfter.TempC
            SmClockBeforeMHz  = $telemetryBefore.SmClockMHz
            SmClockAfterMHz   = $telemetryAfter.SmClockMHz
            PowerBeforeW      = $telemetryBefore.PowerW
            PowerAfterW       = $telemetryAfter.PowerW
            ValidationBad     = $valBad
            Log               = $logCopy
            RawCsv            = $rawPath
        }
        foreach ($key in $runStats.Keys) { $runRow[$key] = $runStats[$key] }
        [void]$runRows.Add([pscustomobject]$runRow)

        $status = if ($valid) { "PASS" } else { "FAIL" }
        Write-Step ("run {0}/{1} {2} exit={3} valid={4} frames={5}" -f ($runIndex + 1), $Runs, $runId, $res.ExitCode, $valid, $frameRows.Count)
    }
}

$runRows | Export-Csv -Path (Join-Path $OutDir "runs.csv") -NoTypeInformation -Encoding UTF8
$allFrames | Export-Csv -Path (Join-Path $OutDir "frames.csv") -NoTypeInformation -Encoding UTF8

# ---------------------------------------------------------------------------
# Aggregation: within-run (frames) and between-run (independent processes).
# ---------------------------------------------------------------------------
$between = New-Object System.Collections.Generic.List[object]
$within = New-Object System.Collections.Generic.List[object]
$speedup = New-Object System.Collections.Generic.List[object]
$validRuns = @($runRows | Where-Object { $_.Valid })
$groups = $validRuns | Group-Object Mode, Draws, Lists, RequestedWorkers

foreach ($group in $groups) {
    $first = $group.Group[0]
    foreach ($metric in $metrics) {
        $runMedians = @($group.Group | ForEach-Object { [double]$_.($metric + "_Median") })
        $betweenStats = Get-Stats -Values $runMedians
        [void]$between.Add([pscustomobject]@{
            Mode    = $first.Mode
            Draws   = $first.Draws
            Lists   = $first.Lists
            Workers = $first.RequestedWorkers
            Metric  = $metric
            Runs    = $betweenStats.Count
            Median  = $betweenStats.Median
            IQR     = $betweenStats.IQR
            P5      = $betweenStats.P5
            P95     = $betweenStats.P95
            Min     = $betweenStats.Min
            Max     = $betweenStats.Max
            Mean    = $betweenStats.Mean
        })

        $runIqrs = @($group.Group | ForEach-Object { [double]$_.($metric + "_IQR") })
        $runP95 = @($group.Group | ForEach-Object { [double]$_.($metric + "_P95") })
        [void]$within.Add([pscustomobject]@{
            Mode          = $first.Mode
            Draws         = $first.Draws
            Lists         = $first.Lists
            Workers       = $first.RequestedWorkers
            Metric        = $metric
            Runs          = $group.Count
            MedianOfIQR   = (Get-Stats -Values $runIqrs).Median
            MedianOfP95   = (Get-Stats -Values $runP95).Median
        })
    }
}

# Speedup against a legacy baseline with the same draw count.
$legacyMedian = @{}
foreach ($row in $between) {
    if ($row.Mode -eq "legacy" -and $row.Metric -eq "recording_ms") {
        $legacyMedian[[int]$row.Draws] = [double]$row.Median
    }
}
foreach ($row in $between) {
    if ($row.Metric -ne "recording_ms" -or $row.Mode -eq "legacy") { continue }
    if (-not $legacyMedian.ContainsKey([int]$row.Draws)) { continue }
    $baseline = [double]$legacyMedian[[int]$row.Draws]
    $speed = if ($row.Median -gt 0) { $baseline / [double]$row.Median } else { $null }
    $run = $validRuns | Where-Object {
        $_.Mode -eq $row.Mode -and $_.Draws -eq $row.Draws -and
        $_.Lists -eq $row.Lists -and $_.RequestedWorkers -eq $row.Workers
    } | Select-Object -First 1
    $participants = if ($run) { [int]$run.TotalParticipants } else { 1 }
    $width = if ($row.Mode -eq "parallel") { [math]::Min([int]$row.Lists, $participants) } else { 1 }
    $efficiency = if ($speed -and $width -gt 0) { $speed / $width } else { $null }
    [void]$speedup.Add([pscustomobject]@{
        Mode                  = $row.Mode
        Draws                 = $row.Draws
        Lists                 = $row.Lists
        RequestedWorkers      = $row.Workers
        RecordingMedianMs     = $row.Median
        LegacyRecordingMedianMs = $baseline
        Speedup               = $speed
        ParallelWidth         = $width
        ParallelEfficiency    = $efficiency
        ActualWorkers         = if ($run) { $run.ActualWorkers } else { 0 }
        TotalParticipants     = $participants
    })
}

$between | Export-Csv -Path (Join-Path $OutDir "between.csv") -NoTypeInformation -Encoding UTF8
$within | Export-Csv -Path (Join-Path $OutDir "within.csv") -NoTypeInformation -Encoding UTF8
$speedup | Export-Csv -Path (Join-Path $OutDir "speedup.csv") -NoTypeInformation -Encoding UTF8

# Stability flag per point (recording metric only).
$stability = New-Object System.Collections.Generic.List[object]
foreach ($row in $between) {
    if ($row.Metric -ne "recording_ms") { continue }
    $ratio = if ($row.Median -gt 0) { [double]$row.IQR / [double]$row.Median } else { $null }
    [void]$stability.Add([pscustomobject]@{
        Mode        = $row.Mode
        Draws       = $row.Draws
        Lists       = $row.Lists
        Workers     = $row.Workers
        MedianMs    = $row.Median
        IQRMs       = $row.IQR
        IQRRatio    = $ratio
        Stable      = ($null -ne $ratio -and $ratio -le $MaxIqrRatio)
    })
}

$invalid = @($runRows | Where-Object { -not $_.Valid })
$pointCounts = @($validRuns | Group-Object Mode, Draws, Lists, RequestedWorkers)
$underfilled = @($pointCounts | Where-Object { $_.Count -lt $Runs })

$summary = [pscustomobject]@{
    schema      = "f354b-1"
    generatedAt = (Get-Date).ToString("o")
    fingerprint = $fingerprint
    plan        = [pscustomobject]@{
        points         = $points.Count
        runsRequested  = $Runs
        warmupFrames   = $WarmupFrames
        measuredFrames = $MeasuredFrames
        totalFrames    = $totalFrames
    }
    counts = [pscustomobject]@{
        runsTotal        = $runRows.Count
        runsValid        = $validRuns.Count
        runsInvalid      = $invalid.Count
        correctnessCases = $correctness.Count
        correctnessFails = @($correctness | Where-Object { -not $_.Pass }).Count
        underfilledPoints = $underfilled.Count
    }
    statisticalUnit = "independent process run; frames are descriptive within-run samples, not independent experiments"
    bootstrap       = "not computed in the pilot; a full study must resample run medians, never individual frames"
    correctness = $correctness
    between     = $between
    within      = $within
    speedup     = $speedup
    stability   = $stability
    runs        = $runRows
    notes       = @(
        "Performance runs use -nodebug -nogbv; correctness runs use Debug Layer + GBV.",
        "Speedup requires the same draws and the same observed resolution as the legacy baseline.",
        "Owner thread always participates; totalParticipants = actual background workers + 1.",
        "ParallelWidth = min(lists, totalParticipants); parallel efficiency = speedup / ParallelWidth.",
        "Frame interval is measured after Present on a hidden window and is not game FPS.",
        "Each configuration runs in a separate process; order is rotated between repetitions."
    )
}
$summary | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $OutDir "summary.json") -Encoding UTF8

Write-Step ("Done. OutDir: " + $OutDir)
Write-Step ("Valid runs: {0}/{1}; underfilled points: {2}" -f $validRuns.Count, $runRows.Count, $underfilled.Count)

$badStability = @($stability | Where-Object { -not $_.Stable })
if ($invalid.Count -gt 0 -or $underfilled.Count -gt 0) {
    throw "Pilot acceptance failed: invalid runs=$($invalid.Count), underfilled points=$($underfilled.Count)."
}
if ($badStability.Count -gt 0) {
    Write-Warning ("Points above IQR/median threshold {0}: {1}" -f $MaxIqrRatio, $badStability.Count)
}
