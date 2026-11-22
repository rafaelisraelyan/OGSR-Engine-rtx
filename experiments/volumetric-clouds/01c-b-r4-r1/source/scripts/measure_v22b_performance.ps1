<#
.SYNOPSIS
    V2.2b GPU timestamp matrix with repeats and median.

.DESCRIPTION
    Runs OGSRCloudsTest --frames=120 --warmup=20 for each resolution/preset/step
    combination, repeats it, and stores every average plus the median. Laptop
    GPU clocks vary between runs, so a single sample is not a stable number.

.PARAMETER Exe
    Release executable.

.PARAMETER EvidenceDir
    Output directory for logs and summary files.
#>
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\build-v22\Release\OGSRCloudsTest.exe"),
    [string]$EvidenceDir = (Join-Path $PSScriptRoot "..\artifacts\v22b_final_evidence"),
    [int]$Repeats = 3
)

$ErrorActionPreference = "Stop"
$invariant = [System.Globalization.CultureInfo]::InvariantCulture
[System.Threading.Thread]::CurrentThread.CurrentCulture = $invariant
[System.Threading.Thread]::CurrentThread.CurrentUICulture = $invariant
if (-not (Test-Path $Exe)) { throw "executable not found: $Exe" }
$Exe = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force $EvidenceDir | Out-Null

function Invoke-Perf([string]$tag, [string[]]$arguments) {
    Write-Host $tag
    $cloud = @()
    $composite = @()
    $total = @()
    for ($i = 1; $i -le $Repeats; ++$i) {
        $name = if ($i -eq 1) { "$tag.log" } else { "$tag`_run$i.log" }
        $log = Join-Path $EvidenceDir $name
        & $Exe @arguments *> $log
        if ($LASTEXITCODE -ne 0) { throw "$tag run $i exited $LASTEXITCODE" }
        $avg = (Select-String -Path $log -Pattern "gpu timestamps average over" |
                Select-Object -Last 1).Line
        if (-not $avg) { throw "$tag run $i produced no timestamp summary" }
        if ($avg -notmatch "over 100 samples") {
            throw "$tag run $i did not produce exactly 100 post-warmup samples: $avg"
        }
        if ($avg -match "cloud ([0-9.]+) ms, composite ([0-9.]+) ms, total ([0-9.]+) ms") {
            $cloud += [double]$matches[1]
            $composite += [double]$matches[2]
            $total += [double]$matches[3]
        }
        else {
            throw "$tag run $i has an unexpected summary: $avg"
        }
    }
    $sorted = $cloud | Sort-Object
    $median = $sorted[[int][math]::Floor($sorted.Count / 2)]
    $lines = @(
        $tag,
        ("  runs cloud ms: " + (($cloud | ForEach-Object { $_.ToString("0.000", $invariant) }) -join ", ")),
        ("  runs composite ms: " + (($composite | ForEach-Object { $_.ToString("0.000", $invariant) }) -join ", ")),
        ("  runs total ms: " + (($total | ForEach-Object { $_.ToString("0.000", $invariant) }) -join ", ")),
        ("  median cloud {0} ms, composite {1} ms, total {2} ms" -f `
            $median.ToString("0.000", $invariant),
            (($composite | Sort-Object)[[int][math]::Floor($composite.Count / 2)]).ToString("0.000", $invariant),
            (($total | Sort-Object)[[int][math]::Floor($total.Count / 2)]).ToString("0.000", $invariant))
    )
    return $lines
}

$half = @()
foreach ($resolution in @(@("720p", "1280x720"), @("1080p", "1920x1080"))) {
    foreach ($preset in @("clear", "partly", "overcast", "storm")) {
        foreach ($steps in @(32, 64, 128)) {
            $tag = "perf_{0}_{1}_{2}" -f $resolution[0], $preset, $steps
            $half += Invoke-Perf $tag @(
                "--mode=clouds", "--window=$($resolution[1])", "--preset=$preset",
                "--steps=$steps", "--frames=120", "--warmup=20", "--no-vsync",
                "--cloud-scale=half")
        }
    }
}
$half | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "performance.txt")
Write-Output "half-resolution matrix: PASS"

$full = @()
foreach ($resolution in @(@("720p", "1280x720"), @("1080p", "1920x1080"))) {
    foreach ($preset in @("clear", "overcast", "storm")) {
        foreach ($steps in @(32, 128)) {
            $tag = "perf_full_{0}_{1}_{2}" -f $resolution[0], $preset, $steps
            $full += Invoke-Perf $tag @(
                "--mode=clouds", "--window=$($resolution[1])", "--preset=$preset",
                "--steps=$steps", "--frames=120", "--warmup=20", "--no-vsync",
                "--cloud-scale=full")
        }
    }
}
$full | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "performance_full.txt")
Write-Output "full-resolution matrix: PASS"
