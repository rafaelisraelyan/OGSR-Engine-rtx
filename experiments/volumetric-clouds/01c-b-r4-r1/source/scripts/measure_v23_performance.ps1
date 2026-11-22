<#
.SYNOPSIS
    V2.3 GPU timestamp matrix with burn-in, interleaved repeats and clocks.

.DESCRIPTION
    The laptop RTX 5070 varies by up to 3x between sessions because of thermal
    and power limits. This script:
      1. burns in the GPU for a fixed time;
      2. interleaves configurations (repeat outer loop) so thermal drift hits
         every configuration equally;
      3. records SM clock / power / temperature around each run;
      4. reports the median and a median with obvious throttle outliers
         removed.

    Warmup contract: every run uses --frames=120 --warmup=20 and must report
    exactly 100 samples.
#>
param(
    [string]$Exe = "",
    [string]$EvidenceDir = "",
    [int]$Repeats = 3,
    [int]$BurnSeconds = 30,
    [string]$Filter = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $root "build-v22\Release\OGSRCloudsTest.exe"
}
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $root "artifacts\v23_final_evidence"
}
if (-not (Test-Path $Exe)) { throw "executable not found: $Exe" }
$Exe = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force $EvidenceDir | Out-Null
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

function Get-GpuState {
    if (-not (Get-Command nvidia-smi -ErrorAction SilentlyContinue)) { return "n/a" }
    $value = & nvidia-smi --query-gpu=clocks.sm,power.draw,temperature.gpu `
        --format=csv,noheader 2>$null
    return (($value -join " ") -replace ",", "/")
}

if ($BurnSeconds -gt 0) {
    Write-Output "burn-in: ${BurnSeconds}s"
    $burn = Start-Process -FilePath $Exe -PassThru -RedirectStandardOutput `
        (Join-Path $EvidenceDir "burn_in.log") -ArgumentList @(
        "--mode=clouds", "--preset=overcast", "--window=1920x1080",
        "--cloud-scale=full", "--steps=128", "--frames=5000", "--no-vsync")
    Start-Sleep -Seconds $BurnSeconds
    if (-not $burn.HasExited) { $burn.Kill() }
}

$configs = @()
foreach ($profile in @("performance", "balanced", "quality", "reference")) {
    foreach ($preset in @("clear", "overcast", "storm")) {
        $configs += @{
            Tag = "perf_1080p_{0}_{1}" -f $preset, $profile
            Args = @("--mode=clouds", "--window=1920x1080", "--preset=$preset",
                     "--profile=$profile", "--frames=120", "--warmup=20",
                     "--no-vsync")
        }
    }
}
foreach ($steps in @(32, 64, 96, 128)) {
    foreach ($preset in @("clear", "overcast")) {
        $configs += @{
            Tag = "perf_1080p_{0}_steps{1}" -f $preset, $steps
            Args = @("--mode=clouds", "--window=1920x1080", "--cloud-scale=half",
                     "--preset=$preset", "--steps=$steps", "--shadow-steps=6",
                     "--frames=120", "--warmup=20", "--no-vsync")
        }
    }
}
foreach ($shadow in @(1, 2, 4, 6)) {
    foreach ($preset in @("clear", "overcast")) {
        $configs += @{
            Tag = "perf_1080p_{0}_shadow{1}" -f $preset, $shadow
            Args = @("--mode=clouds", "--window=1920x1080", "--cloud-scale=half",
                     "--preset=$preset", "--steps=128", "--shadow-steps=$shadow",
                     "--frames=120", "--warmup=20", "--no-vsync")
        }
    }
}

$samples = @{}
$clocks = @{}
foreach ($config in $configs) {
    if ($Filter -and $config.Tag -notlike "*$Filter*") { continue }
    $samples[$config.Tag] = @()
    $clocks[$config.Tag] = @()
}

for ($repeat = 1; $repeat -le $Repeats; ++$repeat) {
    foreach ($config in $configs) {
        if ($Filter -and $config.Tag -notlike "*$Filter*") { continue }
        $name = if ($repeat -eq 1) { "$($config.Tag).log" }
                else { "$($config.Tag)_run$repeat.log" }
        $log = Join-Path $EvidenceDir $name
        $before = Get-GpuState
        & $Exe @($config.Args) *> $log
        if ($LASTEXITCODE -ne 0) {
            throw "$($config.Tag) repeat $repeat exited $LASTEXITCODE"
        }
        $after = Get-GpuState
        $avg = (Select-String -Path $log -Pattern "gpu timestamps average over" |
                Select-Object -Last 1).Line
        if (-not $avg) { throw "$($config.Tag) repeat $repeat produced no summary" }
        if ($avg -notmatch "over 100 samples") {
            throw "$($config.Tag) repeat $repeat did not produce 100 samples"
        }
        if ($avg -match "cloud ([0-9.]+) ms, composite ([0-9.]+) ms, total ([0-9.]+) ms") {
            $samples[$config.Tag] += [double]::Parse($matches[1], $invariant)
            $clocks[$config.Tag] += ("{0} -> {1}" -f $before, $after)
        }
        else {
            throw "$($config.Tag) repeat $repeat has an unexpected summary: $avg"
        }
        Write-Host ("{0} #{1}: {2:0.000} ms" -f $config.Tag, $repeat,
                    $samples[$config.Tag][-1])
    }
}

$lines = @()
foreach ($config in $configs) {
    $values = @($samples[$config.Tag] | Sort-Object)
    if ($values.Count -eq 0) { continue }
    $median = $values[[int][math]::Floor($values.Count / 2)]
    $minimum = ($values | Measure-Object -Minimum).Minimum
    $trimmed = @($values | Where-Object { $_ -le $minimum * 1.5 })
    if ($trimmed.Count -eq 0) { $trimmed = $values }
    $trimmedMedian = ($trimmed | Sort-Object)[[int][math]::Floor($trimmed.Count / 2)]
    $lines += $config.Tag
    $lines += ("  runs cloud ms: " + (($values | ForEach-Object { $_.ToString("0.000", $invariant) }) -join ", "))
    $lines += ("  gpu clock/power/temp: " + ($clocks[$config.Tag] -join " | "))
    $lines += ("  median cloud {0} ms; without throttle outliers {1} ms" -f `
        $median.ToString("0.000", $invariant),
        $trimmedMedian.ToString("0.000", $invariant))
}
$lines | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "performance_v23.txt")
Write-Output "performance matrix: PASS"
