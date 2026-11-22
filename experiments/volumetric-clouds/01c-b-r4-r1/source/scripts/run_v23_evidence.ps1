<#
.SYNOPSIS
    V2.3 lighting, atmosphere, temporal and DX12 evidence.

.DESCRIPTION
    Produces:
      - V0/V1/V2 regression logs and selftests;
      - hardware / GBV / WARP / negative validation matrix;
      - lighting A/B per cloud type (baseline, no shadow, sun only,
        ambient only, final, low sun, atmosphere off);
      - deterministic frame sequences for jitter and camera motion;
      - full capture sets for 720p half, 720p full and 1080p half.

    The V2.2b baseline directory is optional; when present it is copied into
    the lighting A/B sheets for a direct visual comparison.
#>
param(
    [string]$Exe = "",
    [string]$DebugExe = "",
    [string]$EvidenceDir = "",
    [string]$CaptureDir = "",
    [string]$LightingDir = "",
    [string]$SequenceDir = "",
    [string]$BaselineDir = "",
    [switch]$SkipPerformance,
    [switch]$SkipVisual
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $root "build-v23-final\Release\OGSRCloudsTest.exe"
}
if ([string]::IsNullOrWhiteSpace($DebugExe)) {
    $DebugExe = Join-Path $root "build-v23-final\Debug\OGSRCloudsTest.exe"
}
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $root "artifacts\v23_final_evidence"
}
if ([string]::IsNullOrWhiteSpace($CaptureDir)) {
    $CaptureDir = Join-Path $root "artifacts\v23_final_capture"
}
if ([string]::IsNullOrWhiteSpace($LightingDir)) {
    $LightingDir = Join-Path $root "artifacts\v23_lighting"
}
if ([string]::IsNullOrWhiteSpace($SequenceDir)) {
    $SequenceDir = Join-Path $root "artifacts\v23_sequences"
}
if ([string]::IsNullOrWhiteSpace($BaselineDir)) {
    $BaselineDir = Join-Path $root "artifacts\v22b_final_capture\720p_full_128"
}
New-Item -ItemType Directory -Force $EvidenceDir | Out-Null

# Never silently run a stale binary. Both executables must exist; their paths,
# SHA256, size and timestamp are written to binary_identity.txt and printed.
function Get-BinaryIdentity([string]$path, [string]$role) {
    if (-not (Test-Path $path)) {
        throw ("{0} executable not found: {1}. Pass -Exe/-DebugExe explicitly " +
               "or build the V2.3 final configuration." -f $role, $path)
    }
    $item = Get-Item $path
    $hash = (Get-FileHash $item.FullName -Algorithm SHA256).Hash
    return [pscustomobject]@{
        Role = $role
        Path = $item.FullName
        Sha256 = $hash
        Bytes = $item.Length
        Timestamp = $item.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")
    }
}

$releaseIdentity = Get-BinaryIdentity $Exe "Release"
$debugIdentity = Get-BinaryIdentity $DebugExe "Debug"
$Exe = $releaseIdentity.Path
$DebugExe = $debugIdentity.Path
$identityLines = @(
    "V2.3 evidence binary identity",
    ("release path: " + $releaseIdentity.Path),
    ("release sha256: " + $releaseIdentity.Sha256),
    ("release bytes: " + $releaseIdentity.Bytes),
    ("release timestamp: " + $releaseIdentity.Timestamp),
    ("debug path: " + $debugIdentity.Path),
    ("debug sha256: " + $debugIdentity.Sha256),
    ("debug bytes: " + $debugIdentity.Bytes),
    ("debug timestamp: " + $debugIdentity.Timestamp)
)
$identityLines | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "binary_identity.txt")
$identityLines | ForEach-Object { Write-Output $_ }

function Invoke-Logged([string]$name, [string[]]$arguments) {
    $log = Join-Path $EvidenceDir ($name + ".log")
    & $Exe @arguments 2>&1 | Tee-Object -FilePath $log | Out-Null
    return @{ Name = $name; Exit = $LASTEXITCODE; Log = $log }
}

function Assert-Exit($result, [int]$expected) {
    if ($result.Exit -ne $expected) {
        throw ("{0} exited {1}, expected {2} (see {3})" -f
               $result.Name, $result.Exit, $expected, $result.Log)
    }
}

function Copy-Selftest([string]$name) {
    $source = Join-Path (Split-Path $Exe) "selftest.txt"
    Copy-Item $source (Join-Path $EvidenceDir ($name + ".txt")) -Force
}

function Convert-BmpsToPng([string]$dir) {
    if (-not (Test-Path $dir)) { return }
    Add-Type -AssemblyName System.Drawing
    Get-ChildItem $dir -Filter *.bmp | ForEach-Object {
        $image = [System.Drawing.Image]::FromFile($_.FullName)
        $image.Save((Join-Path $dir ($_.BaseName + ".png")),
                    [System.Drawing.Imaging.ImageFormat]::Png)
        $image.Dispose()
    }
}

function New-GridSheet([string[]]$paths, [string[]]$labels, [string]$outFile,
                       [int]$columns) {
    Add-Type -AssemblyName System.Drawing
    $cellW = 480
    $cellH = 270
    $rows = [int][math]::Ceiling($paths.Count / $columns)
    $sheet = New-Object System.Drawing.Bitmap ($cellW * $columns), ($cellH * $rows)
    $graphics = [System.Drawing.Graphics]::FromImage($sheet)
    $graphics.Clear([System.Drawing.Color]::Black)
    $font = New-Object System.Drawing.Font("Consolas", 14)
    for ($i = 0; $i -lt $paths.Count; ++$i) {
        if (-not (Test-Path $paths[$i])) { continue }
        $image = [System.Drawing.Image]::FromFile($paths[$i])
        $x = ($i % $columns) * $cellW
        $y = [math]::Floor($i / $columns) * $cellH
        $graphics.DrawImage($image, $x, $y, $cellW, $cellH)
        $graphics.DrawString($labels[$i], $font,
                             [System.Drawing.Brushes]::White, $x + 6, $y + 6)
        $image.Dispose()
    }
    $sheet.Save($outFile, [System.Drawing.Imaging.ImageFormat]::Png)
    $font.Dispose()
    $graphics.Dispose()
    $sheet.Dispose()
}

# ---------------------------------------------------------------- unit tests
$testExe = Join-Path (Split-Path $Exe) "OGSRCloudsAuditTests.exe"
& $testExe 2>&1 | Tee-Object -FilePath (Join-Path $EvidenceDir "audit_policy_tests.log") | Out-Null
if ($LASTEXITCODE -ne 0) { throw "audit policy unit tests failed" }
Write-Output "audit policy tests: PASS"

# ---------------------------------------------------------------- V0 slices
$r = Invoke-Logged "v0_slices_selftest_debug" @(
    "--mode=slices", "--selftest", "--size=128", "--debug")
Assert-Exit $r 0
Copy-Selftest "selftest_v0_slices_debug"
Write-Output "V0 slices selftest: PASS"

$v0Capture = Join-Path $EvidenceDir "v0_capture"
if (Test-Path $v0Capture) { Remove-Item -Recurse -Force $v0Capture }
$r = Invoke-Logged "v0_slices_capture" @(
    "--mode=slices", "--size=128", "--capture=$v0Capture")
Assert-Exit $r 0
if (@(Get-ChildItem $v0Capture -Filter *.bmp).Count -ne 10) {
    throw "V0 capture did not produce 10 slices"
}
Convert-BmpsToPng $v0Capture
Write-Output "V0 slices capture: PASS (10 BMP)"

# ---------------------------------------------------------------- selftests
$selftests = @(
    @{ Name = "v23_hw128_debug"; Exe = $Exe;
       Args = @("--mode=clouds", "--selftest", "--size=128", "--debug") },
    @{ Name = "v23_hw128_gbv"; Exe = $Exe;
       Args = @("--mode=clouds", "--selftest", "--size=128", "--gpu-validation") },
    @{ Name = "v23_warp64"; Exe = $Exe;
       Args = @("--mode=clouds", "--selftest", "--size=64", "--warp") },
    @{ Name = "v23_nodbg64"; Exe = $Exe;
       Args = @("--mode=clouds", "--selftest", "--size=64") },
    @{ Name = "v23_debug_build"; Exe = $DebugExe;
       Args = @("--mode=clouds", "--selftest", "--size=64", "--debug") },
    @{ Name = "v23_debug_build_gbv"; Exe = $DebugExe;
       Args = @("--mode=clouds", "--selftest", "--size=64", "--gpu-validation") }
)
foreach ($test in $selftests) {
    $log = Join-Path $EvidenceDir ($test.Name + ".log")
    & $test.Exe @($test.Args) 2>&1 | Tee-Object -FilePath $log | Out-Null
    $exit = $LASTEXITCODE
    $selftest = Join-Path (Split-Path $test.Exe) "selftest.txt"
    Copy-Item $selftest (Join-Path $EvidenceDir ("selftest_" + $test.Name + ".txt")) -Force
    if ($exit -ne 0) { throw "$($test.Name) failed (exit $exit)" }
    $text = Get-Content -Raw (Join-Path $EvidenceDir ("selftest_" + $test.Name + ".txt"))
    foreach ($pattern in @("V2 CLOUD SELFTEST: PASS",
                           "sun optical depth is bounded",
                           "shadow density matches primary density",
                           "near-horizontal sun stays finite",
                           "sun below horizon removes direct",
                           "final validation after cloud GPU work",
                           "weather map seam check: PASS",
                           "OVERALL SELFTEST: PASS")) {
        if ($text -notmatch [regex]::Escape($pattern)) {
            throw "$($test.Name) missing '$pattern'"
        }
    }
    if ($text -notmatch "final debug errors: 0") {
        throw "$($test.Name) reported final debug errors"
    }
    if (($test.Name -match "debug|gbv") -and
        $text -notmatch "final validation verdict: PASS") {
        throw "$($test.Name) did not report final validation PASS"
    }
    Write-Output "$($test.Name): PASS"
}

# ---------------------------------------------------------------- GBV frames
$r = Invoke-Logged "v23_gbv_frames120" @(
    "--mode=clouds", "--size=128", "--gpu-validation", "--frames=120", "--no-vsync")
Assert-Exit $r 0
$gbvText = Get-Content -Raw $r.Log
if ($gbvText -notmatch "debug message totals: 0 errors, 0 warnings") {
    throw "GBV 120-frame run reported debug messages"
}
if ($gbvText -notmatch "gpu timestamps average over 120 samples") {
    throw "GBV 120-frame run did not report 120 samples"
}
Write-Output "GBV actual frame path: PASS"

# The auditor explicitly requires the clean Debug build to run the actual
# cloud compute+composite frame path, not only the selftest readback.
$debugFramesLog = Join-Path $EvidenceDir "v23_debug_gbv_frames120.log"
& $DebugExe --mode=clouds --size=128 --gpu-validation --frames=120 `
    --no-vsync *> $debugFramesLog
if ($LASTEXITCODE -ne 0) {
    throw "Debug GBV actual frames exited $LASTEXITCODE"
}
$debugFramesText = Get-Content -Raw $debugFramesLog
foreach ($pattern in @(
        "D3D12 debug layer enabled (GPU-based validation ON)",
        "validation preflight:",
        "cloud frame path: CloudRayMarcher compute + CloudCompositor draw active",
        "gpu timestamps average over 120 samples",
        "debug message totals: 0 errors, 0 warnings",
        "application finished with code 0",
        "GPU idle",
        "live-object messages emitted:",
        "non-device objects: 0")) {
    if ($debugFramesText -notmatch [regex]::Escape($pattern)) {
        throw "Debug GBV actual frames missing '$pattern'"
    }
}
if ($debugFramesText -notmatch "gpu requested=1 enabled=1") {
    throw "Debug GBV actual frames did not report GPU validation enabled"
}
$debugHash = @(
    "Debug GBV actual frames",
    ("exe path: " + $debugIdentity.Path),
    ("exe sha256: " + $debugIdentity.Sha256),
    ("log: " + $debugFramesLog)
)
$debugHash | Set-Content -Encoding utf8 `
    (Join-Path $EvidenceDir "debug_gbv_frames120_sha256.txt")
Write-Output "Debug GBV actual frame path: PASS"

# ---------------------------------------------------------------- resize GBV
$r = Invoke-Logged "v23_resize_gbv" @(
    "--mode=clouds", "--size=64", "--gpu-validation", "--frames=90",
    "--resize-test", "--no-vsync")
Assert-Exit $r 0
$resizeText = Get-Content -Raw $r.Log
if ($resizeText -notmatch "cloud output resized") {
    throw "resize run did not resize the cloud output"
}
Write-Output "resize with GBV: PASS"

# ---------------------------------------------------------- negative matrix
function Test-Negative([string]$name, [string[]]$arguments,
                       [string]$captureDir = "") {
    $r = Invoke-Logged $name $arguments
    if ($r.Exit -ne 3) {
        throw ("{0} exited {1}, expected 3" -f $name, $r.Exit)
    }
    $text = Get-Content -Raw $r.Log
    if ($text -match "V2 CLOUD SELFTEST: PASS") {
        throw "$name incorrectly reported PASS"
    }
    if ($text -notmatch "INCONCLUSIVE") {
        throw "$name did not report INCONCLUSIVE"
    }
    if ($captureDir -and (Test-Path $captureDir) -and
        @(Get-ChildItem $captureDir -File).Count -gt 0) {
        throw "$name created capture artifacts"
    }
}

$negativeCapture = Join-Path $EvidenceDir "negative_capture_artifacts"
if (Test-Path $negativeCapture) { Remove-Item -Recurse -Force $negativeCapture }
Test-Negative "negative_selftest" @(
    "--mode=clouds", "--selftest", "--size=64", "--debug",
    "--test-no-validation")
Test-Negative "negative_gbv_selftest" @(
    "--mode=clouds", "--selftest", "--size=64", "--gpu-validation",
    "--test-no-validation")
Test-Negative "negative_capture" @(
    "--mode=clouds", "--capture=$negativeCapture", "--size=64", "--debug",
    "--test-no-validation") $negativeCapture
Test-Negative "negative_frames" @(
    "--mode=clouds", "--frames=10", "--debug", "--test-no-validation")
Test-Negative "negative_no_info_queue" @(
    "--mode=clouds", "--selftest", "--size=64", "--debug",
    "--test-no-info-queue")
Test-Negative "negative_gbv_no_info_queue" @(
    "--mode=clouds", "--selftest", "--size=64", "--gpu-validation",
    "--test-no-info-queue")
Test-Negative "negative_combined" @(
    "--mode=clouds", "--selftest", "--capture=$negativeCapture", "--size=64",
    "--debug", "--test-no-validation") $negativeCapture

function Test-InjectedError([string]$name, [string[]]$arguments) {
    $r = Invoke-Logged $name $arguments
    if ($r.Exit -ne 1) {
        throw ("{0} exited {1}, expected 1 (FAIL)" -f $name, $r.Exit)
    }
    # The console log truncates the multi-line report; selftest.txt is the
    # authoritative final report.
    $report = Join-Path (Split-Path $Exe) "selftest.txt"
    $text = Get-Content -Raw $report
    Copy-Item $report `
        (Join-Path $EvidenceDir "selftest_negative_injected_error.txt") -Force
    if ($text -match "OVERALL SELFTEST: PASS") {
        throw "$name incorrectly reported PASS"
    }
    if ($text -notmatch "final validation verdict: FAIL") {
        throw "$name did not report FAIL"
    }
    if ($text -notmatch "final debug errors: [1-9]") {
        throw "$name did not count the injected error"
    }
    $logText = Get-Content -Raw $r.Log
    if ($logText -notmatch "debug message totals: [1-9][0-9]* errors") {
        throw "$name shutdown totals did not include the error"
    }
}
Test-InjectedError "negative_injected_error" @(
    "--mode=clouds", "--selftest", "--size=64", "--debug",
    "--test-inject-debug-error")
Write-Output "negative validation matrix: PASS"

# ---------------------------------------------------------- lighting A/B
if (-not $SkipVisual) {
if (Test-Path $LightingDir) { Remove-Item -Recurse -Force $LightingDir }
New-Item -ItemType Directory -Force $LightingDir | Out-Null
$lightingScenes = @(
    @{ Name = "cumulus"; Scene = "clouds_cumulus_below" },
    @{ Name = "stratocumulus"; Scene = "clouds_stratocumulus_below" },
    @{ Name = "overcast"; Scene = "clouds_overcast_base" },
    @{ Name = "storm"; Scene = "clouds_storm_aerial" }
)
$lightingVariants = @(
    @{ Name = "v22b_baseline"; Args = @() },
    @{ Name = "no_shadow"; Args = @("--ablation=no-shadow") },
    @{ Name = "sun_only"; Args = @("--lighting=sun-only") },
    @{ Name = "ambient_only"; Args = @("--lighting=ambient-only") },
    @{ Name = "final"; Args = @() },
    @{ Name = "low_sun"; Args = @("--sun-elevation=5") },
    @{ Name = "atmosphere_off"; Args = @("--atmosphere=off") }
)
$lightingSummary = @()
foreach ($scene in $lightingScenes) {
    $paths = @()
    $labels = @()
    foreach ($variant in $lightingVariants) {
        $dir = Join-Path $LightingDir ($variant.Name)
        New-Item -ItemType Directory -Force $dir | Out-Null
        $target = Join-Path $dir ($scene.Scene + ".png")
        if ($variant.Name -eq "v22b_baseline") {
            $baseline = Join-Path $BaselineDir ($scene.Scene + ".png")
            if (Test-Path $baseline) {
                Copy-Item $baseline $target -Force
            }
        }
        else {
            $variantArgs = @("--mode=clouds", "--window=1280x720", "--cloud-scale=full",
                      "--steps=128", "--scene=$($scene.Scene)",
                      "--capture=$dir") + $variant.Args
            $r = Invoke-Logged ("lighting_{0}_{1}" -f $scene.Name, $variant.Name) $variantArgs
            Assert-Exit $r 0
            Convert-BmpsToPng $dir
        }
        if (Test-Path $target) {
            $paths += $target
            $labels += ("{0} / {1}" -f $scene.Name, $variant.Name)
        }
    }
    $sheet = Join-Path $LightingDir ("sheet_" + $scene.Name + ".png")
    New-GridSheet $paths $labels $sheet 4
    $lightingSummary += "scene $($scene.Name): $($paths.Count) variants -> $sheet"
}
$lightingSummary | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "lighting_ab.txt")
Write-Output "lighting A/B: PASS"

# ---------------------------------------------------------- atmosphere A/B
$atmosphereScenes = @("clouds_clear_horizon", "clouds_storm_horizon",
                      "clouds_overcast_aerial", "clouds_cumulus_below")
$atmospherePaths = @()
$atmosphereLabels = @()
foreach ($scene in $atmosphereScenes) {
    foreach ($mode in @("on", "off")) {
        $dir = Join-Path $LightingDir ("atmosphere_" + $mode)
        New-Item -ItemType Directory -Force $dir | Out-Null
        $r = Invoke-Logged ("atmosphere_{0}_{1}" -f $scene, $mode) @(
            "--mode=clouds", "--window=1280x720", "--cloud-scale=full",
            "--steps=128", "--scene=$scene", "--atmosphere=$mode",
            "--capture=$dir")
        Assert-Exit $r 0
        Convert-BmpsToPng $dir
        $atmospherePaths += (Join-Path $dir ($scene + ".png"))
        $atmosphereLabels += ("{0} / atmosphere {1}" -f $scene, $mode)
    }
}
New-GridSheet $atmospherePaths $atmosphereLabels `
    (Join-Path $LightingDir "sheet_atmosphere.png") 2
Write-Output "atmosphere A/B: PASS"

# ---------------------------------------------------------- sun path series
$sunPathPaths = @()
$sunPathLabels = @()
foreach ($elevation in @(5, 34, 75)) {
    $dir = Join-Path $LightingDir ("sun_path_" + $elevation)
    New-Item -ItemType Directory -Force $dir | Out-Null
    $r = Invoke-Logged ("sun_path_{0}" -f $elevation) @(
        "--mode=clouds", "--window=1280x720", "--cloud-scale=full",
        "--steps=128", "--scene=clouds_shadow_path_debug",
        "--sun-elevation=$elevation", "--capture=$dir")
    Assert-Exit $r 0
    Convert-BmpsToPng $dir
    $sunPathPaths += (Join-Path $dir "clouds_shadow_path_debug.png")
    $sunPathLabels += ("sun elevation {0} deg" -f $elevation)
}
New-GridSheet $sunPathPaths $sunPathLabels `
    (Join-Path $LightingDir "sheet_sun_path.png") 3
Write-Output "sun path series: PASS"

# ------------------------------------------------------- sun horizon series
$sunOnlyPaths = @()
$sunOnlyLabels = @()
foreach ($elevation in @(-10, -5, 0, 5, 34, 75)) {
    $dir = Join-Path $LightingDir ("sun_only_" + $elevation)
    New-Item -ItemType Directory -Force $dir | Out-Null
    $r = Invoke-Logged ("sun_only_{0}" -f $elevation) @(
        "--mode=clouds", "--window=1280x720", "--cloud-scale=full",
        "--steps=128", "--scene=clouds_cumulus_below",
        "--sun-elevation=$elevation", "--lighting=sun-only",
        "--atmosphere=off", "--capture=$dir")
    Assert-Exit $r 0
    Convert-BmpsToPng $dir
    $sunOnlyPaths += (Join-Path $dir "clouds_cumulus_below.png")
    $sunOnlyLabels += ("sun-only {0} deg" -f $elevation)
}
New-GridSheet $sunOnlyPaths $sunOnlyLabels `
    (Join-Path $LightingDir "sheet_sun_horizon.png") 3
Write-Output "sun horizon series: PASS"

# ---------------------------------------------------------- sequences
if (Test-Path $SequenceDir) { Remove-Item -Recurse -Force $SequenceDir }
New-Item -ItemType Directory -Force $SequenceDir | Out-Null
$sequenceBase = @("--mode=clouds", "--preset=clear", "--window=1280x720",
                  "--cloud-scale=half", "--steps=128",
                  "--camera=0,100,-1200", "--camera-pitch=20",
                  "--frames=30", "--no-vsync")
$sequences = @(
    @{ Name = "static_spatial"; Args = @("--motion=static", "--wind=off", "--jitter=spatial") },
    @{ Name = "static_off"; Args = @("--motion=static", "--wind=off", "--jitter=off") },
    @{ Name = "pan_slow_spatial"; Args = @("--motion=pan-slow", "--wind=off", "--jitter=spatial") },
    @{ Name = "pan_slow_off"; Args = @("--motion=pan-slow", "--wind=off", "--jitter=off") },
    @{ Name = "pan_fast_spatial"; Args = @("--motion=pan-fast", "--wind=off", "--jitter=spatial") },
    @{ Name = "up_spatial"; Args = @("--motion=up", "--wind=off", "--jitter=spatial") },
    @{ Name = "fly_spatial"; Args = @("--motion=fly", "--wind=off", "--jitter=spatial") },
    @{ Name = "pan_slow_wind"; Args = @("--motion=pan-slow", "--wind=on", "--jitter=spatial") },
    @{ Name = "steps_32"; Args = @("--motion=pan-slow", "--wind=off", "--jitter=spatial", "--steps=32") },
    @{ Name = "steps_64"; Args = @("--motion=pan-slow", "--wind=off", "--jitter=spatial", "--steps=64") },
    @{ Name = "steps_96"; Args = @("--motion=pan-slow", "--wind=off", "--jitter=spatial", "--steps=96") },
    @{ Name = "pan_slow_full"; Args = @("--motion=pan-slow", "--wind=off", "--jitter=spatial", "--cloud-scale=full") }
)
$temporal = @()
foreach ($sequence in $sequences) {
    $dir = Join-Path $SequenceDir $sequence.Name
    $sequenceArgs = $sequenceBase + $sequence.Args + @("--sequence=$dir")
    $r = Invoke-Logged ("sequence_" + $sequence.Name) $sequenceArgs
    Assert-Exit $r 0
    $metrics = Join-Path $dir "sequence_metrics.txt"
    if (-not (Test-Path $metrics)) { throw "$($sequence.Name) produced no metrics" }
    Convert-BmpsToPng $dir
    $summary = Select-String -Path $metrics -Pattern "mean high-frequency|mean frame-to-frame"
    $temporal += ("{0}`n{1}" -f $sequence.Name, (($summary | ForEach-Object { $_.Line }) -join "`n"))
}
$temporal | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "temporal_metrics.txt")
Write-Output "temporal sequences: PASS"

# ---------------------------------------------------------- perf matrix
$perfScript = Join-Path $PSScriptRoot "measure_v23_performance.ps1"
if (-not $SkipPerformance -and (Test-Path $perfScript)) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $perfScript `
        -Exe $Exe -EvidenceDir $EvidenceDir
    if ($LASTEXITCODE -ne 0) { throw "performance matrix failed" }
    Write-Output "performance matrix: PASS"
}

# ---------------------------------------------------------- capture sets
if (Test-Path $CaptureDir) { Remove-Item -Recurse -Force $CaptureDir }
New-Item -ItemType Directory -Force $CaptureDir | Out-Null
$captureSets = @(
    @{ Name = "720p_half"; Args = @("--window=1280x720", "--cloud-scale=half", "--steps=128") },
    @{ Name = "720p_full_128"; Args = @("--window=1280x720", "--cloud-scale=full", "--steps=128") },
    @{ Name = "1080p_half"; Args = @("--window=1920x1080", "--cloud-scale=half", "--steps=128") }
)
foreach ($set in $captureSets) {
    $dir = Join-Path $CaptureDir $set.Name
    $captureArgs = @("--mode=clouds") + $set.Args + @("--capture=$dir")
    $r = Invoke-Logged ("capture_" + $set.Name) $captureArgs
    Assert-Exit $r 0
    $count = @(Get-ChildItem $dir -Filter *.bmp).Count
    if ($count -lt 35) { throw "$($set.Name) produced $count images, expected 35+" }
    Convert-BmpsToPng $dir
}
New-GridSheet @(
    (Join-Path $CaptureDir "720p_full_128\clouds_cumulus_below.png"),
    (Join-Path $CaptureDir "720p_full_128\clouds_stratocumulus_below.png"),
    (Join-Path $CaptureDir "720p_full_128\clouds_overcast_base.png"),
    (Join-Path $CaptureDir "720p_full_128\clouds_storm_aerial.png"),
    (Join-Path $CaptureDir "720p_full_128\clouds_clear_horizon.png"),
    (Join-Path $CaptureDir "720p_full_128\clouds_storm_horizon.png")
) @("cumulus", "stratocumulus", "overcast", "storm", "clear horizon", "storm horizon") `
  (Join-Path $CaptureDir "contact_sheet.png") 3
Write-Output "capture sets: PASS"
}

Write-Output "V2.3 evidence written to $EvidenceDir"
