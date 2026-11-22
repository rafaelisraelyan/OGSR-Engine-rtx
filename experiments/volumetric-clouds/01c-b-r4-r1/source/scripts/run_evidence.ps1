param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$EvidenceDir,
    [Parameter(Mandatory = $true)][string]$CaptureDir,
    [string]$DebugExe = ""
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($DebugExe)) { $DebugExe = $Exe }
$Exe = (Resolve-Path $Exe).Path
$DebugExe = (Resolve-Path $DebugExe).Path
New-Item -ItemType Directory -Force $EvidenceDir | Out-Null
$EvidenceDir = (Resolve-Path $EvidenceDir).Path

function Invoke-Selftest([string]$binary, [string]$name, [string[]]$arguments) {
    $log = Join-Path $EvidenceDir "run_selftest_$name.log"
    & $binary @arguments 2>&1 | Tee-Object -FilePath $log | Out-Null
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "selftest $name failed with exit code $code (see $log)" }
    $source = Join-Path (Split-Path $binary) "selftest.txt"
    if (-not (Test-Path $source)) { throw "selftest $name did not produce $source" }
    Copy-Item $source (Join-Path $EvidenceDir "selftest_$name.txt") -Force
    $verdict = Select-String -Path $log -Pattern "V0 SELFTEST:" | Select-Object -Last 1
    Write-Output ("{0}: {1}" -f $name, $verdict.Line.Trim())
}

Invoke-Selftest $Exe "hw128" @("--selftest", "--debug", "--size=128")
Invoke-Selftest $Exe "hw64" @("--selftest", "--debug", "--size=64")
Invoke-Selftest $Exe "gbv128" @("--selftest", "--debug", "--gpu-validation", "--size=128")
Invoke-Selftest $Exe "gbv64" @("--selftest", "--debug", "--gpu-validation", "--size=64")
Invoke-Selftest $Exe "warp64" @("--selftest", "--debug", "--warp", "--size=64")
Invoke-Selftest $Exe "unorm64" @("--selftest", "--debug", "--size=64", "--format=unorm")
if ($DebugExe -ne $Exe) {
    Invoke-Selftest $DebugExe "debug64" @("--selftest", "--debug", "--size=64")
}

$testExe = Join-Path (Split-Path $Exe) "OGSRCloudsAuditTests.exe"
if (-not (Test-Path $testExe)) { throw "audit unit test executable not found: $testExe" }
$unitLog = Join-Path $EvidenceDir "audit_policy_tests.log"
& $testExe 2>&1 | Tee-Object -FilePath $unitLog | Out-Null
if ($LASTEXITCODE -ne 0) { throw "audit policy unit tests failed (see $unitLog)" }
Write-Output "audit policy tests: PASS"

function Invoke-ExpectInconclusive([string]$name, [string[]]$arguments) {
    $log = Join-Path $EvidenceDir "run_negative_$name.log"
    & $Exe @arguments 2>&1 | Tee-Object -FilePath $log | Out-Null
    if ($LASTEXITCODE -ne 3) {
        throw "negative test $name must exit 3, got $LASTEXITCODE (see $log)"
    }
    $verdict = Select-String -Path $log -Pattern "V0 SELFTEST: INCONCLUSIVE"
    Write-Output ("negative {0}: exit 3{1}" -f $name, ($(if ($verdict) { ", INCONCLUSIVE" } else { "" })))
    return $log
}

$negativeLog = Invoke-ExpectInconclusive "validation" @("--selftest", "--debug", "--gpu-validation", "--size=64", "--test-no-validation")
Copy-Item (Join-Path (Split-Path $Exe) "selftest.txt") (Join-Path $EvidenceDir "selftest_negative_no_validation.txt") -Force

$negativeCaptureDir = Join-Path $EvidenceDir "negative-capture"
if (Test-Path $negativeCaptureDir) { Remove-Item -Recurse -Force $negativeCaptureDir }
Invoke-ExpectInconclusive "validation_capture" @("--capture=$negativeCaptureDir", "--debug", "--test-no-validation") | Out-Null
if (Test-Path $negativeCaptureDir) { throw "negative capture test unexpectedly created $negativeCaptureDir" }

$negativeGpuCaptureDir = Join-Path $EvidenceDir "negative-capture-gpu"
if (Test-Path $negativeGpuCaptureDir) { Remove-Item -Recurse -Force $negativeGpuCaptureDir }
Invoke-ExpectInconclusive "validation_capture_gpu" @("--capture=$negativeGpuCaptureDir", "--debug", "--gpu-validation", "--test-no-validation") | Out-Null
if (Test-Path $negativeGpuCaptureDir) { throw "negative GPU capture test unexpectedly created $negativeGpuCaptureDir" }

Invoke-ExpectInconclusive "frames" @("--debug", "--test-no-validation", "--frames=1") | Out-Null

Invoke-ExpectInconclusive "no_info_queue" @("--selftest", "--debug", "--test-no-info-queue", "--size=64") | Out-Null
Copy-Item (Join-Path (Split-Path $Exe) "selftest.txt") (Join-Path $EvidenceDir "selftest_negative_no_infoqueue.txt") -Force

Invoke-ExpectInconclusive "gpu_no_info_queue" @("--selftest", "--debug", "--gpu-validation", "--test-no-info-queue", "--size=64") | Out-Null
Copy-Item (Join-Path (Split-Path $Exe) "selftest.txt") (Join-Path $EvidenceDir "selftest_negative_gpu_no_infoqueue.txt") -Force

$negativeCombinedDir = Join-Path $EvidenceDir "negative-combined"
if (Test-Path $negativeCombinedDir) { Remove-Item -Recurse -Force $negativeCombinedDir }
Invoke-ExpectInconclusive "combined" @("--selftest", "--capture=$negativeCombinedDir", "--debug", "--test-no-validation") | Out-Null
if (Test-Path $negativeCombinedDir) { throw "combined negative test unexpectedly created $negativeCombinedDir" }
Copy-Item (Join-Path (Split-Path $Exe) "selftest.txt") (Join-Path $EvidenceDir "selftest_negative_combined.txt") -Force

Write-Output "negative validation matrix: PASS (all exit 3, no capture artifacts)"

if (Test-Path $CaptureDir) { Remove-Item -Recurse -Force $CaptureDir }
New-Item -ItemType Directory -Force $CaptureDir | Out-Null
$captureLog = Join-Path $EvidenceDir "run_capture_hw128.log"
& $Exe "--capture=$CaptureDir" "--debug" "--size=128" 2>&1 | Tee-Object -FilePath $captureLog | Out-Null
if ($LASTEXITCODE -ne 0) { throw "capture failed with exit code $LASTEXITCODE (see $captureLog)" }
Copy-Item (Join-Path $CaptureDir "capture_info.txt") (Join-Path $EvidenceDir "capture_info.txt") -Force

Add-Type -AssemblyName System.Drawing
Get-ChildItem $CaptureDir -Filter *.bmp | ForEach-Object {
    $image = [System.Drawing.Image]::FromFile($_.FullName)
    $png = Join-Path $CaptureDir ($_.BaseName + ".png")
    $image.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    $image.Dispose()
    Remove-Item $_.FullName
}
$count = @(Get-ChildItem $CaptureDir -Filter *.png).Count
Write-Output "capture: $count PNG files"

$resizeLog = Join-Path $EvidenceDir "run_resize_hw128.log"
& $Exe "--frames=90" "--resize-test" "--debug" "--size=128" 2>&1 | Tee-Object -FilePath $resizeLog | Out-Null
if ($LASTEXITCODE -ne 0) { throw "resize test failed with exit code $LASTEXITCODE (see $resizeLog)" }
Write-Output "resize: PASS"

Write-Output "Evidence written to $EvidenceDir"
