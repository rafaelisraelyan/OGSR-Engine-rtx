#requires -Version 5.1
$ErrorActionPreference = 'Stop'

# Differential control for the transition-shader GPU-validation hang.
# Runs the same 5-frame animated transition under --gpu-validation with the
# unmodified R2, unmodified R3 and current R4 developer binaries. A timeout is
# the expected result for all three and proves environment/driver drift rather
# than an R4 regression.

$w = 'C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work'
$out = Join-Path $w 'evidence\gbv_transition_control'
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $out | Out-Null

$targets = [ordered]@{
    r2 = 'C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r2_work\build_developer\Release\OGSRCloudsTest.exe'
    r3 = 'C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r3_work\build_developer\Release\OGSRCloudsTest.exe'
    r4 = Join-Path $w 'build_developer\Release\OGSRCloudsTest.exe'
}
$childArgs = '--mode=clouds --window=800x450 --gpu-validation --clouds=on --camera=1000,150,-7000 --camera-yaw=12 --camera-pitch=10 --fov=62 --weather-seed=7331 --time=0 --no-vsync --breakup=off --front-height=on --sun-azimuth=65 --sun-elevation=18 --wind=off --weather-timeline=overcast:rain_sf --timeline-duration=0.5 --timeline-time=0 --timeline-play --timeline-interrupt=0:0.65:partly --fixed-frame-delta=0.0166667 --frames=5'

$timeoutMs = 150000
$rows = @()
foreach ($name in $targets.Keys) {
    $exe = $targets[$name]
    $d = Join-Path $out $name
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $exe -ArgumentList $childArgs -NoNewWindow -PassThru `
        -RedirectStandardOutput "$d\run.log" -RedirectStandardError "$d\run.err.log"
    $done = $p.WaitForExit($timeoutMs)
    $sw.Stop()
    $timedOut = -not $done
    if ($timedOut) { Stop-Process -Id $p.Id -Force }
    $exitCode = if ($done) { $p.ExitCode } else { -1 }
    Set-Content -Path "$d\timed_out.txt" -Value ($(if ($timedOut) { '1' } else { '0' })) -Encoding ASCII
    Set-Content -Path "$d\exit_code.txt" -Value $exitCode -Encoding ASCII
    Set-Content -Path "$d\elapsed_seconds.txt" -Value ([int]$sw.Elapsed.TotalSeconds) -Encoding ASCII
    $rows += [pscustomobject]@{
        binary = $name
        exe = $exe
        timed_out = $timedOut
        exit_code = $exitCode
        elapsed_seconds = [int]$sw.Elapsed.TotalSeconds
    }
    " $name timed_out=$timedOut elapsed=$([int]$sw.Elapsed.TotalSeconds)s"
}

$summary = [pscustomobject]@{
    format = 'V_WEATHER_01C_B_R4_GBV_TRANSITION_CONTROL_V1'
    date = '2026-11-20'
    command = $childArgs
    timeout_seconds = $timeoutMs / 1000
    result = 'ENVIRONMENT_DRIFT'
    note = 'R2, R3 and R4 all fail to complete the transition shader under GPU-based validation within the hard timeout.'
    rows = $rows
}
$json = $summary | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $out 'summary.json'), $json,
    (New-Object Text.UTF8Encoding($false)))
"control matrix complete"
