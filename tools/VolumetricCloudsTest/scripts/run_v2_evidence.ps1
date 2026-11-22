param(
    [string]$Exe = "",
    [string]$DebugExe = "",
    [string]$EvidenceDir = "",
    [string]$CaptureDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $root "build-clean\Release\OGSRCloudsTest.exe"
}
if ([string]::IsNullOrWhiteSpace($DebugExe)) {
    $DebugExe = Join-Path $root "build-clean\Debug\OGSRCloudsTest.exe"
}
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $root "artifacts\v2_evidence"
}
if ([string]::IsNullOrWhiteSpace($CaptureDir)) {
    $CaptureDir = Join-Path $root "artifacts\v2_capture"
}
New-Item -ItemType Directory -Force $EvidenceDir | Out-Null
if (Test-Path $CaptureDir) { Remove-Item -Recurse -Force $CaptureDir }
New-Item -ItemType Directory -Force $CaptureDir | Out-Null

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

function New-ContactSheet([string]$dir, [string]$outFile,
                          [string[]]$names) {
    Add-Type -AssemblyName System.Drawing
    $cellW = 640
    $cellH = 360
    $sheet = New-Object System.Drawing.Bitmap ($cellW * 3), ($cellH * 2)
    $graphics = [System.Drawing.Graphics]::FromImage($sheet)
    $graphics.Clear([System.Drawing.Color]::Black)
    for ($i = 0; $i -lt $names.Count; ++$i) {
        $path = Join-Path $dir $names[$i]
        if (-not (Test-Path $path)) { continue }
        $image = [System.Drawing.Image]::FromFile($path)
        $x = ($i % 3) * $cellW
        $y = [math]::Floor($i / 3) * $cellH
        $graphics.DrawImage($image, $x, $y, $cellW, $cellH)
        $image.Dispose()
    }
    $sheet.Save($outFile, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $sheet.Dispose()
}

$testExe = Join-Path (Split-Path $Exe) "OGSRCloudsAuditTests.exe"
if (-not (Test-Path $testExe)) {
    throw "audit unit test executable not found: $testExe"
}
$unitLog = Join-Path $EvidenceDir "audit_policy_tests.log"
& $testExe 2>&1 | Tee-Object -FilePath $unitLog | Out-Null
if ($LASTEXITCODE -ne 0) { throw "audit policy unit tests failed" }
Write-Output "audit policy tests: PASS"

# V0 regression.
$r = Invoke-Logged "v0_slices_selftest_debug" @(
    "--mode=slices", "--selftest", "--size=128", "--debug")
Assert-Exit $r 0
Copy-Selftest "selftest_v0_slices_debug"
Write-Output "V0 slices selftest: PASS"

$v0Capture = Join-Path $EvidenceDir "v0_capture"
if (Test-Path $v0Capture) { Remove-Item -Recurse -Force $v0Capture }
$r = Invoke-Logged "v0_slices_capture" @(
    "--mode=slices", "--size=64", "--capture=$v0Capture")
Assert-Exit $r 0
$v0Count = @(Get-ChildItem $v0Capture -Filter *.bmp).Count
if ($v0Count -ne 10) { throw "V0 capture produced $v0Count images, expected 10" }
Write-Output "V0 slices capture: PASS (10 BMP)"

# V2.1 correctness.
$r = Invoke-Logged "v2_hw128_debug_selftest" @(
    "--mode=clouds", "--selftest", "--size=128", "--debug")
Assert-Exit $r 0
Copy-Selftest "selftest_v2_hw128_debug"
$selftestText = Get-Content -Raw (Join-Path $EvidenceDir "selftest_v2_hw128_debug.txt")
foreach ($pattern in @("V2 CLOUD SELFTEST: PASS", "weather map is finite",
                       "weather sampling is world-space",
                       "cloud type changes", "weather parameter interpolation")) {
    if ($selftestText -notmatch [regex]::Escape($pattern)) {
        throw "V2 selftest report missing: $pattern"
    }
}
Write-Output "V2 hardware 128 debug: PASS"

$r = Invoke-Logged "v2_hw128_gbv_selftest" @(
    "--mode=clouds", "--selftest", "--size=128", "--gpu-validation")
Assert-Exit $r 0
Copy-Selftest "selftest_v2_hw128_gbv"
Write-Output "V2 hardware 128 GBV: PASS"

$r = Invoke-Logged "v2_warp64_selftest" @(
    "--mode=clouds", "--selftest", "--size=64", "--warp")
Assert-Exit $r 0
Copy-Selftest "selftest_v2_warp64"
Write-Output "V2 WARP 64: PASS"

$r = Invoke-Logged "v2_nodbg64_selftest" @(
    "--mode=clouds", "--selftest", "--size=64")
Assert-Exit $r 0
Copy-Selftest "selftest_v2_nodbg64"
Write-Output "V2 no-debug 64: PASS"

# Actual frame path with GPU-based validation and weather generation.
$gbvFrames = Invoke-Logged "v2_gbv_frames120" @(
    "--mode=clouds", "--size=128", "--gpu-validation", "--frames=120",
    "--no-vsync")
Assert-Exit $gbvFrames 0
$gbvText = Get-Content -Raw $gbvFrames.Log
foreach ($pattern in @("gpu requested=1 enabled=1",
                       "weather map generated",
                       "cloud frame path: CloudRayMarcher compute + CloudCompositor draw active",
                       "gpu timestamps average",
                       "debug message totals: 0 errors, 0 warnings",
                       "application finished with code 0")) {
    if ($gbvText -notmatch [regex]::Escape($pattern)) {
        throw "GBV frame log missing: $pattern"
    }
}
Write-Output "V2 GBV actual frame path (120 frames): PASS"

$resize = Invoke-Logged "v2_resize_gbv" @(
    "--mode=clouds", "--size=64", "--gpu-validation", "--frames=90",
    "--resize-test", "--no-vsync")
Assert-Exit $resize 0
$resizeText = Get-Content -Raw $resize.Log
foreach ($pattern in @("resize test: requesting", "cloud output resized",
                       "debug message totals: 0 errors, 0 warnings",
                       "application finished with code 0")) {
    if ($resizeText -notmatch [regex]::Escape($pattern)) {
        throw "Resize log missing: $pattern"
    }
}
Write-Output "V2 resize with GBV: PASS"

# Visual captures: half and full cloud output, 720p and 1080p.
$capSpecs = @(
    @{ Name = "capture_720p_half"; Dir = (Join-Path $CaptureDir "720p_half");
       Args = @("--mode=clouds", "--window=1280x720", "--cloud-scale=half") },
    @{ Name = "capture_720p_full"; Dir = (Join-Path $CaptureDir "720p_full");
       Args = @("--mode=clouds", "--window=1280x720", "--cloud-scale=full") },
    @{ Name = "capture_1080p_half"; Dir = (Join-Path $CaptureDir "1080p_half");
       Args = @("--mode=clouds", "--window=1920x1080", "--cloud-scale=half") }
)
foreach ($spec in $capSpecs) {
    $argsList = @($spec.Args) + @("--capture", $spec.Dir)
    $r = Invoke-Logged $spec.Name $argsList
    Assert-Exit $r 0
    $count = @(Get-ChildItem $spec.Dir -Filter *.bmp).Count
    if ($count -lt 20) { throw "$($spec.Name) produced $count images, expected 20+" }
    Convert-BmpsToPng $spec.Dir
}
New-ContactSheet (Join-Path $CaptureDir "720p_full") (Join-Path $CaptureDir "contact_sheet.png") @(
    "clouds_clear_horizon.png", "clouds_partly_horizon.png",
    "clouds_overcast_horizon.png", "clouds_rain_horizon.png",
    "clouds_storm_aerial.png", "clouds_weather_debug.png")
Write-Output "V2 captures: PASS (half/full, 720p/1080p)"

# GPU timestamp matrix, 120 frames after 20-frame warmup.
$perf = @()
foreach ($res in @(@("720p", "1280x720"), @("1080p", "1920x1080"))) {
    foreach ($preset in @("clear", "partly", "overcast", "storm")) {
        foreach ($steps in @(32, 48, 64)) {
            $tag = "perf_$($res[0])_$($preset)_$($steps)"
            $r = Invoke-Logged $tag @(
                "--mode=clouds", "--window=$($res[1])", "--preset=$preset",
                "--steps=$steps", "--frames=120", "--warmup=20", "--no-vsync")
            Assert-Exit $r 0
            $avg = (Select-String -Path $r.Log -Pattern "gpu timestamps average over" |
                    Select-Object -Last 1).Line
            $dist = (Select-String -Path $r.Log -Pattern "gpu timestamps distribution" |
                     Select-Object -Last 1).Line
            if (-not $avg -or -not $dist) {
                throw "$tag produced no timestamp summary"
            }
            $perf += "$tag`n$avg`n$dist"
        }
    }
}
$perf | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "performance.txt")
Write-Output "V2 GPU timestamp matrix: PASS"

# Negative validation matrix: exit 3 and no capture artifacts.
function Test-Negative([string]$name, [string[]]$arguments,
                       [string]$captureToCheck) {
    $r = Invoke-Logged $name $arguments
    if ($r.Exit -ne 3) {
        throw ("{0} exited {1}, expected 3" -f $name, $r.Exit)
    }
    $text = Get-Content -Raw $r.Log
    if ($text -match "V2 CLOUD SELFTEST: PASS") {
        throw "$name incorrectly reported PASS"
    }
    if ($captureToCheck -and (Test-Path $captureToCheck)) {
        if (@(Get-ChildItem $captureToCheck -Recurse -File).Count -ne 0) {
            throw "$name created capture artifacts"
        }
    }
}
$negativeCapture = Join-Path $EvidenceDir "negative_capture"
if (Test-Path $negativeCapture) { Remove-Item -Recurse -Force $negativeCapture }
Test-Negative "negative_frames" @(
    "--mode=clouds", "--debug", "--test-no-validation", "--frames=1") ""
Test-Negative "negative_capture" @(
    "--mode=clouds", "--debug", "--test-no-validation",
    "--capture=$negativeCapture") $negativeCapture
Test-Negative "negative_selftest" @(
    "--mode=clouds", "--debug", "--test-no-validation", "--selftest",
    "--size=64") ""
Test-Negative "negative_combined" @(
    "--mode=clouds", "--debug", "--test-no-validation", "--selftest",
    "--capture=$negativeCapture", "--size=64") $negativeCapture
Test-Negative "negative_no_info_queue" @(
    "--mode=clouds", "--debug", "--test-no-info-queue", "--selftest",
    "--size=64") ""
Write-Output "V2 negative validation matrix: PASS"

Write-Output "V2 evidence written to $EvidenceDir"
