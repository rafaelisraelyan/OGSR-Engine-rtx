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
    $EvidenceDir = Join-Path $root "artifacts\v1_evidence"
}
if ([string]::IsNullOrWhiteSpace($CaptureDir)) {
    $CaptureDir = Join-Path $root "artifacts\v1_capture"
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

$testExe = Join-Path (Split-Path $Exe) "OGSRCloudsAuditTests.exe"
if (-not (Test-Path $testExe)) {
    throw "audit unit test executable not found: $testExe"
}
$unitLog = Join-Path $EvidenceDir "audit_policy_tests.log"
& $testExe 2>&1 | Tee-Object -FilePath $unitLog | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "audit policy unit tests failed"
}
Write-Output "audit policy tests: PASS"

# V0 regression: slices and the accepted noise selftest still work.
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

# V1 correctness: hardware, GPU-based validation, WARP, no-debug path.
$r = Invoke-Logged "v1_hw128_debug_selftest" @(
    "--mode=clouds", "--selftest", "--size=128", "--debug")
Assert-Exit $r 0
Copy-Selftest "selftest_v1_hw128_debug"
Write-Output "V1 hardware 128 debug: PASS"

$r = Invoke-Logged "v1_hw128_gbv_selftest" @(
    "--mode=clouds", "--selftest", "--size=128", "--gpu-validation")
Assert-Exit $r 0
Copy-Selftest "selftest_v1_hw128_gbv"
Write-Output "V1 hardware 128 GBV: PASS"

$r = Invoke-Logged "v1_warp64_selftest" @(
    "--mode=clouds", "--selftest", "--size=64", "--warp")
Assert-Exit $r 0
Copy-Selftest "selftest_v1_warp64"
Write-Output "V1 WARP 64: PASS"

$r = Invoke-Logged "v1_nodbg64_selftest" @(
    "--mode=clouds", "--selftest", "--size=64")
Assert-Exit $r 0
Copy-Selftest "selftest_v1_nodbg64"
Write-Output "V1 no-debug 64: PASS (NOT REQUESTED path)"

# V1-A02: GPU-based validation on the actual frame path, not only selftest.
$gbvFrames = Invoke-Logged "v1_gbv_frames120" @(
    "--mode=clouds", "--size=128", "--gpu-validation", "--frames=120",
    "--no-vsync")
Assert-Exit $gbvFrames 0
$gbvText = Get-Content -Raw $gbvFrames.Log
if ($gbvText -notmatch "gpu requested=1 enabled=1") {
    throw "GBV frame run did not report active GPU validation"
}
if ($gbvText -notmatch "cloud frame path: CloudRayMarcher compute \+ CloudCompositor draw active") {
    throw "GBV frame run did not execute the cloud frame path"
}
if ($gbvText -notmatch "gpu timestamps average") {
    throw "GBV frame run produced no cloud/composite timestamps"
}
if ($gbvText -notmatch "debug message totals: 0 errors, 0 warnings") {
    throw "GBV frame run produced debug errors/warnings"
}
Write-Output "V1 GBV actual frame path (120 frames): PASS"

# Reproducible temporal captures for the wind fix.
$temporal0 = Join-Path $EvidenceDir "temporal_t0"
$temporal1 = Join-Path $EvidenceDir "temporal_t1"
foreach ($dir in @($temporal0, $temporal1)) {
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
}
$r = Invoke-Logged "v1_temporal_t0" @(
    "--mode=clouds", "--preset=clear", "--time=0", "--capture=$temporal0")
Assert-Exit $r 0
$r = Invoke-Logged "v1_temporal_t1" @(
    "--mode=clouds", "--preset=clear", "--time=1", "--capture=$temporal1")
Assert-Exit $r 0
$hash0 = (Get-FileHash (Join-Path $temporal0 "clouds_clear_horizon.bmp") -Algorithm SHA256).Hash
$hash1 = (Get-FileHash (Join-Path $temporal1 "clouds_clear_horizon.bmp") -Algorithm SHA256).Hash
if ($hash0 -eq $hash1) {
    throw "temporal captures t=0 and t=1 are identical"
}
Write-Output "V1 temporal captures t=0/t=1: PASS"

# Resize/minimize machinery through the regular frame path.
$r = Invoke-Logged "v1_resize" @(
    "--mode=clouds", "--frames=90", "--resize-test", "--size=64")
Assert-Exit $r 0
Write-Output "V1 resize: PASS"

# V1 visual captures: 720p Clear Day and 1080p Overcast at 32 steps.
$r = Invoke-Logged "v1_capture_720p_clear" @(
    "--mode=clouds", "--window=1280x720", "--preset=clear",
    "--capture=$CaptureDir")
Assert-Exit $r 0
$capture720 = @(Get-ChildItem $CaptureDir -Filter *.bmp).Count
if ($capture720 -lt 9) { throw "V1 720p capture produced $capture720 images" }

$capture1080 = Join-Path $EvidenceDir "capture_1080p_overcast"
if (Test-Path $capture1080) { Remove-Item -Recurse -Force $capture1080 }
$r = Invoke-Logged "v1_capture_1080p_overcast" @(
    "--mode=clouds", "--window=1920x1080", "--preset=overcast",
    "--steps=32", "--capture=$capture1080")
Assert-Exit $r 0
$capture1080Count = @(Get-ChildItem $capture1080 -Filter *.bmp).Count
if ($capture1080Count -lt 9) {
    throw "V1 1080p capture produced $capture1080Count images"
}
Write-Output "V1 captures: PASS"

function Convert-BmpsToPng([string]$directory) {
    Add-Type -AssemblyName System.Drawing
    Get-ChildItem $directory -Filter *.bmp | ForEach-Object {
        $image = [System.Drawing.Image]::FromFile($_.FullName)
        $target = Join-Path $directory ($_.BaseName + ".png")
        $image.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
        $image.Dispose()
    }
}
Convert-BmpsToPng $CaptureDir
Convert-BmpsToPng $capture1080
Convert-BmpsToPng $temporal0
Convert-BmpsToPng $temporal1

# 2x2 review contact sheet for the 720p Clear Day set.
Add-Type -AssemblyName System.Drawing
$contactNames = @(
    "clouds_clear_horizon.png",
    "clouds_clear_upward.png",
    "clouds_overcast_horizon.png",
    "clouds_camera_moved.png"
)
$contactLabels = @(
    "Clear Day - horizon",
    "Clear Day - upward",
    "Overcast - horizon",
    "Overcast - moved camera"
)
$contact = New-Object System.Drawing.Bitmap 2560, 1440
$graphics = [System.Drawing.Graphics]::FromImage($contact)
$graphics.Clear([System.Drawing.Color]::Black)
$font = New-Object System.Drawing.Font("Segoe UI", 22, [System.Drawing.FontStyle]::Bold)
$brush = [System.Drawing.Brushes]::White
$background = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(170, 0, 0, 0))
for ($i = 0; $i -lt 4; ++$i) {
    $path = Join-Path $CaptureDir $contactNames[$i]
    if (-not (Test-Path $path)) { continue }
    $image = [System.Drawing.Image]::FromFile($path)
    $x = ($i % 2) * 1280
    $y = [Math]::Floor($i / 2) * 720
    $graphics.DrawImage($image, $x, $y, 1280, 720)
    $graphics.FillRectangle($background, $x + 10, $y + 10, 470, 42)
    $graphics.DrawString($contactLabels[$i], $font, $brush, $x + 16, $y + 14)
    $image.Dispose()
}
$graphics.Dispose()
$contact.Save((Join-Path $CaptureDir "contact_sheet.png"),
              [System.Drawing.Imaging.ImageFormat]::Png)
$contact.Dispose()
$background.Dispose()
$font.Dispose()

# Negative validation matrix: every requested-but-unavailable path exits 3.
$negativeFrames = Invoke-Logged "v1_negative_frames" @(
    "--mode=clouds", "--debug", "--test-no-validation", "--frames=1")
Assert-Exit $negativeFrames 3

$negativeGpuSelftest = Invoke-Logged "v1_negative_gpu_selftest" @(
    "--mode=clouds", "--selftest", "--debug", "--gpu-validation",
    "--size=64", "--test-no-validation")
Assert-Exit $negativeGpuSelftest 3
Copy-Selftest "selftest_v1_negative_gpu_no_validation"

$negativeCapture = Join-Path $EvidenceDir "negative_cloud_capture"
if (Test-Path $negativeCapture) { Remove-Item -Recurse -Force $negativeCapture }
$negativeCaptureResult = Invoke-Logged "v1_negative_capture" @(
    "--mode=clouds", "--debug", "--test-no-validation",
    "--capture=$negativeCapture")
Assert-Exit $negativeCaptureResult 3
if (Test-Path $negativeCapture) {
    throw "negative cloud capture unexpectedly created $negativeCapture"
}

$negativeCombined = Join-Path $EvidenceDir "negative_combined"
if (Test-Path $negativeCombined) { Remove-Item -Recurse -Force $negativeCombined }
$negativeCombinedResult = Invoke-Logged "v1_negative_combined" @(
    "--mode=clouds", "--selftest", "--capture=$negativeCombined",
    "--debug", "--test-no-validation")
Assert-Exit $negativeCombinedResult 3
if (Test-Path $negativeCombined) {
    throw "negative combined capture unexpectedly created $negativeCombined"
}
Copy-Selftest "selftest_v1_negative_combined"

$negativeInfoQueue = Invoke-Logged "v1_negative_no_info_queue" @(
    "--mode=clouds", "--selftest", "--debug", "--test-no-info-queue",
    "--size=64")
Assert-Exit $negativeInfoQueue 3
Copy-Selftest "selftest_v1_negative_no_info_queue"

$negativeGpuInfoQueue = Invoke-Logged "v1_negative_gpu_no_info_queue" @(
    "--mode=clouds", "--selftest", "--debug", "--gpu-validation",
    "--test-no-info-queue", "--size=64")
Assert-Exit $negativeGpuInfoQueue 3
Copy-Selftest "selftest_v1_negative_gpu_no_info_queue"

Write-Output "V1 negative validation matrix: PASS (all exit 3, no capture artifacts)"

# Performance matrix: GPU timestamps, not CPU frame time. WARP is excluded
# from performance comparison; it is covered by the correctness selftest.
$performance = @()
$runs = @(
    @{ Name = "hw_720p_clear_32"; Window = "1280x720"; Preset = "clear"; Steps = 32 },
    @{ Name = "hw_720p_clear_64"; Window = "1280x720"; Preset = "clear"; Steps = 64 },
    @{ Name = "hw_720p_overcast_32"; Window = "1280x720"; Preset = "overcast"; Steps = 32 },
    @{ Name = "hw_720p_overcast_64"; Window = "1280x720"; Preset = "overcast"; Steps = 64 },
    @{ Name = "hw_1080p_clear_32"; Window = "1920x1080"; Preset = "clear"; Steps = 32 },
    @{ Name = "hw_1080p_clear_64"; Window = "1920x1080"; Preset = "clear"; Steps = 64 },
    @{ Name = "hw_1080p_overcast_32"; Window = "1920x1080"; Preset = "overcast"; Steps = 32 },
    @{ Name = "hw_1080p_overcast_64"; Window = "1920x1080"; Preset = "overcast"; Steps = 64 }
)
foreach ($run in $runs) {
    $result = Invoke-Logged ("perf_" + $run.Name) @(
        "--mode=clouds", "--window=$($run.Window)",
        "--preset=$($run.Preset)", "--steps=$($run.Steps)",
        "--frames=120", "--no-vsync")
    Assert-Exit $result 0
    $line = (Select-String -Path $result.Log -Pattern "gpu timestamps average").Line
    if (-not $line) { throw "no GPU timestamp summary in $($result.Log)" }
    $performance += ("{0}: {1}" -f $run.Name, $line)
}
$performance | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "performance.txt")
Write-Output "V1 GPU timestamp matrix: PASS"

Write-Output ("V1 evidence written to {0}" -f $EvidenceDir)
