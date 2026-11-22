param(
    [string]$Exe = "",
    [string]$DebugExe = "",
    [string]$EvidenceDir = "",
    [string]$CaptureDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $Exe = Join-Path $root "build-v22\Release\OGSRCloudsTest.exe"
}
if ([string]::IsNullOrWhiteSpace($DebugExe)) {
    $DebugExe = Join-Path $root "build-v22\Debug\OGSRCloudsTest.exe"
}
if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path $root "artifacts\v22b_final_evidence"
}
if ([string]::IsNullOrWhiteSpace($CaptureDir)) {
    $CaptureDir = Join-Path $root "artifacts\v22b_final_capture"
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

function New-ABSheet([string]$v1, [string]$v21, [string]$v22,
                     [string]$outFile, [string[]]$scenes) {
    Add-Type -AssemblyName System.Drawing
    $cellW = 640
    $cellH = 360
    $sheet = New-Object System.Drawing.Bitmap ($cellW * 3), ($cellH * 4)
    $graphics = [System.Drawing.Graphics]::FromImage($sheet)
    $graphics.Clear([System.Drawing.Color]::Black)
    $font = New-Object System.Drawing.Font("Consolas", 18)
    $brush = [System.Drawing.Brushes]::White
    $sources = @($v1, $v21, $v22)
    $labels = @("V1", "V2.1", "V2.2")
    for ($row = 0; $row -lt $scenes.Count; ++$row) {
        for ($col = 0; $col -lt 3; ++$col) {
            $path = Join-Path $sources[$col] $scenes[$row]
            if (-not (Test-Path $path)) { continue }
            $image = [System.Drawing.Image]::FromFile($path)
            $x = $col * $cellW
            $y = $row * $cellH
            $graphics.DrawImage($image, $x, $y, $cellW, $cellH)
            $graphics.DrawString($labels[$col], $font, $brush, $x + 8, $y + 8)
            $image.Dispose()
        }
    }
    $sheet.Save($outFile, [System.Drawing.Imaging.ImageFormat]::Png)
    $font.Dispose()
    $graphics.Dispose()
    $sheet.Dispose()
}

$testExe = Join-Path (Split-Path $Exe) "OGSRCloudsAuditTests.exe"
$unitLog = Join-Path $EvidenceDir "audit_policy_tests.log"
& $testExe 2>&1 | Tee-Object -FilePath $unitLog | Out-Null
if ($LASTEXITCODE -ne 0) { throw "audit policy unit tests failed" }
Write-Output "audit policy tests: PASS"

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

# V2.2b noise and density diagnostics, same seed/camera/time as the captures.
$diag = Join-Path $EvidenceDir "diagnostics"
if (Test-Path $diag) { Remove-Item -Recurse -Force $diag }
New-Item -ItemType Directory -Force $diag | Out-Null
$noiseDir = Join-Path $diag "noise_slices"
$r = Invoke-Logged "noise_slices_128" @(
    "--mode=slices", "--size=128", "--noise-slice-capture=$noiseDir")
Assert-Exit $r 0
if (@(Get-ChildItem $noiseDir -Filter *.bmp).Count -ne 9) {
    throw "noise slice capture did not produce 9 images"
}
Convert-BmpsToPng $noiseDir
$diagSpecs = @(
    @("cumulus_baseline", "clouds_cumulus_below", ""),
    @("cumulus_no_warp", "clouds_cumulus_below", "--ablation=no-warp"),
    @("cumulus_macro_only", "clouds_cumulus_below", "--ablation=macro-only"),
    @("cumulus_no_base", "clouds_cumulus_below", "--ablation=no-base"),
    @("cumulus_no_fine", "clouds_cumulus_below", "--ablation=no-fine"),
    @("cumulus_no_shadow", "clouds_cumulus_below", "--ablation=no-shadow"),
    @("cumulus_vertical_1", "clouds_cumulus_below", "--vertical-noise=1.0"),
    @("cumulus_vertical_6", "clouds_cumulus_below", "--vertical-noise=6.0"),
    @("cumulus_steps_32", "clouds_cumulus_below", "--steps=32"),
    @("cumulus_steps_64", "clouds_cumulus_below", "--steps=64"),
    @("cumulus_steps_128", "clouds_cumulus_below", "--steps=128"),
    @("cumulus_half", "clouds_cumulus_below", "--cloud-scale=half"),
    @("cumulus_64", "clouds_cumulus_below", "--size=64"),
    @("cumulus_256", "clouds_cumulus_below", "--size=256"),
    @("overcast_slice", "clouds_density_slice_overcast", ""),
    @("overcast_slice_no_warp", "clouds_density_slice_overcast", "--ablation=no-warp"),
    @("overcast_slice_macro_only", "clouds_density_slice_overcast", "--ablation=macro-only")
)
foreach ($spec in $diagSpecs) {
    $dir = Join-Path $diag $spec[0]
    $args = @("--mode=clouds", "--window=1280x720", "--cloud-scale=full",
              "--steps=128", "--scene=$($spec[1])", "--capture=$dir")
    if ($spec[2]) { $args += ($spec[2] -split " ") }
    $r = Invoke-Logged ("diag_" + $spec[0]) $args
    Assert-Exit $r 0
    Convert-BmpsToPng $dir
}
Write-Output "V2.2b diagnostics: PASS"

$r = Invoke-Logged "v22_hw128_debug_selftest" @(
    "--mode=clouds", "--selftest", "--size=128", "--debug")
Assert-Exit $r 0
Copy-Selftest "selftest_v22_hw128_debug"
$text = Get-Content -Raw (Join-Path $EvidenceDir "selftest_v22_hw128_debug.txt")
foreach ($pattern in @("V2 CLOUD SELFTEST: PASS",
                       "weather map seam check: PASS",
                       "weather sampler wrap probe max RGBA diff",
                       "weather boundary pan diff",
                       "extreme height-variation",
                       "above-layer and large-world cameras")) {
    if ($text -notmatch [regex]::Escape($pattern)) {
        throw "V2.2 selftest report missing: $pattern"
    }
}
Write-Output "V2.2 hardware 128 debug: PASS"

$r = Invoke-Logged "v22_hw128_gbv_selftest" @(
    "--mode=clouds", "--selftest", "--size=128", "--gpu-validation")
Assert-Exit $r 0
Copy-Selftest "selftest_v22_hw128_gbv"
Write-Output "V2.2 hardware 128 GBV: PASS"

$r = Invoke-Logged "v22_warp64_selftest" @(
    "--mode=clouds", "--selftest", "--size=64", "--warp")
Assert-Exit $r 0
Copy-Selftest "selftest_v22_warp64"
Write-Output "V2.2 WARP 64: PASS"

$r = Invoke-Logged "v22_nodbg64_selftest" @(
    "--mode=clouds", "--selftest", "--size=64")
Assert-Exit $r 0
Copy-Selftest "selftest_v22_nodbg64"
Write-Output "V2.2 no-debug 64: PASS"

$gbvFrames = Invoke-Logged "v22_gbv_frames120" @(
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
Write-Output "V2.2 GBV actual frame path (120 frames): PASS"

$resize = Invoke-Logged "v22_resize_gbv" @(
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
Write-Output "V2.2 resize with GBV: PASS"

$specs = @(
    @{ Name = "capture_720p_half"; Dir = (Join-Path $CaptureDir "720p_half");
       Args = @("--mode=clouds", "--window=1280x720", "--cloud-scale=half",
                "--steps=128") },
    @{ Name = "capture_720p_full_128"; Dir = (Join-Path $CaptureDir "720p_full_128");
       Args = @("--mode=clouds", "--window=1280x720", "--cloud-scale=full",
                "--steps=128") },
    @{ Name = "capture_1080p_half"; Dir = (Join-Path $CaptureDir "1080p_half");
       Args = @("--mode=clouds", "--window=1920x1080", "--cloud-scale=half",
                "--steps=128") }
)
foreach ($spec in $specs) {
    $argsList = @($spec.Args) + @("--capture", $spec.Dir)
    $r = Invoke-Logged $spec.Name $argsList
    Assert-Exit $r 0
    $count = @(Get-ChildItem $spec.Dir -Filter *.bmp).Count
    if ($count -lt 30) { throw "$($spec.Name) produced $count images, expected 30+" }
    Convert-BmpsToPng $spec.Dir
}
New-ContactSheet (Join-Path $CaptureDir "720p_full_128") `
    (Join-Path $CaptureDir "contact_sheet.png") @(
    "clouds_cumulus_below.png", "clouds_cumulus_side.png",
    "clouds_clear_aerial.png", "clouds_overcast_base.png",
    "clouds_storm_aerial.png", "clouds_density_slice_clear.png")

$v1Capture = Join-Path $root "artifacts\v22b_ab\v1"
$v21Capture = Join-Path $root "artifacts\v22b_ab\v21"
$v22Capture = Join-Path $CaptureDir "720p_half"
New-ABSheet $v1Capture $v21Capture $v22Capture `
    (Join-Path $CaptureDir "ab_v1_v21_v22.png") @(
    "clouds_clear_horizon.png", "clouds_clear_upward.png",
    "clouds_overcast_upward.png", "clouds_camera_moved.png")
Write-Output "V2.2b A/B contact sheet: PASS"

$modes = @("none", "no-weather", "no-mid", "no-base", "no-fine",
           "shadow-2", "shadow-4", "no-type", "no-warp", "macro-only",
           "no-shadow")
$ablation = @()
foreach ($preset in @("clear", "storm")) {
    foreach ($mode in $modes) {
        $tag = "ablation_1080p_${preset}_64_$mode"
        $r = Invoke-Logged $tag @(
            "--mode=clouds", "--window=1920x1080", "--preset=$preset",
            "--steps=64", "--frames=120", "--warmup=20", "--no-vsync",
            "--cloud-scale=half", "--ablation=$mode")
        Assert-Exit $r 0
        $avg = (Select-String -Path $r.Log -Pattern "gpu timestamps average over" |
                Select-Object -Last 1).Line
        $dist = (Select-String -Path $r.Log -Pattern "gpu timestamps distribution" |
                 Select-Object -Last 1).Line
        if (-not $avg -or -not $dist) { throw "$tag produced no timestamp summary" }
        if ($avg -notmatch "over 100 samples") {
            throw "$tag did not produce exactly 100 post-warmup samples: $avg"
        }
        $ablation += "$tag`n$avg`n$dist"
    }
}
$ablation | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "ablation.txt")
Write-Output "V2.2b ablation matrix: PASS"

$perf = @()
foreach ($res in @(@("720p", "1280x720"), @("1080p", "1920x1080"))) {
    foreach ($preset in @("clear", "partly", "overcast", "storm")) {
        foreach ($steps in @(32, 64, 128)) {
            $tag = "perf_$($res[0])_$($preset)_$($steps)"
            $r = Invoke-Logged $tag @(
                "--mode=clouds", "--window=$($res[1])", "--preset=$preset",
                "--steps=$steps", "--frames=120", "--warmup=20", "--no-vsync")
            Assert-Exit $r 0
            $avg = (Select-String -Path $r.Log -Pattern "gpu timestamps average over" |
                    Select-Object -Last 1).Line
            $dist = (Select-String -Path $r.Log -Pattern "gpu timestamps distribution" |
                     Select-Object -Last 1).Line
            if (-not $avg -or -not $dist) { throw "$tag produced no timestamp summary" }
            if ($avg -notmatch "over 100 samples") {
                throw "$tag did not produce exactly 100 post-warmup samples: $avg"
            }
            $perf += "$tag`n$avg`n$dist"
        }
    }
}
$perf | Set-Content -Encoding utf8 (Join-Path $EvidenceDir "performance.txt")
Write-Output "V2.2b GPU timestamp matrix: PASS"

function Test-Negative([string]$name, [string[]]$arguments,
                       [string]$captureToCheck) {
    $r = Invoke-Logged $name $arguments
    if ($r.Exit -ne 3) { throw ("{0} exited {1}, expected 3" -f $name, $r.Exit) }
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
Test-Negative "negative_no_info_queue" @(
    "--mode=clouds", "--debug", "--test-no-info-queue", "--selftest",
    "--size=64") ""
Write-Output "V2.2b negative validation matrix: PASS"

Write-Output "V2.2b evidence written to $EvidenceDir"
