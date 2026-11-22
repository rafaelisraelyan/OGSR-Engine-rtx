#requires -Version 5.1
$ErrorActionPreference = 'Stop'

# R4 GBV matrix.
#
# The animated transition shader under --gpu-validation does not complete
# within a 150 s hard timeout on this machine. The unmodified R2 and R3
# control binaries reproduce the same timeout (see
# evidence/gbv_transition_control), so it is environment/driver drift, not an
# R4 change. The GPU-validation matrix therefore covers the standard
# CloudRayMarch shader with clouds on/off, while the transition shader is
# validated with the D3D12 Debug Layer + InfoQueue (no GPU-based validation)
# in transition_debug.

$root = 'C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work'
$exe = Join-Path $root 'build_developer\Release\OGSRCloudsTest.exe'
$evidence = Join-Path $root 'evidence\gbv'
if (Test-Path $evidence) { Remove-Item $evidence -Recurse -Force }
New-Item -ItemType Directory -Force -Path $evidence | Out-Null

$camera = '--camera=1000,150,-7000 --camera-yaw=12 --camera-pitch=10 --fov=62 --weather-seed=7331 --time=0 --no-vsync --breakup=off --front-height=on --sun-azimuth=65 --sun-elevation=18 --wind=off'

function Run-Gbv($name, $argText) {
    $d = Join-Path $evidence $name
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    $proc = Start-Process -FilePath $exe -ArgumentList $argText -NoNewWindow `
        -Wait -PassThru -RedirectStandardOutput "$d\run.log" `
        -RedirectStandardError "$d\run.err.log"
    if ($null -ne $proc.ExitCode -and $proc.ExitCode -ne 0) {
        throw "GBV case failed: $name (exit $($proc.ExitCode))"
    }
    " gbv $name complete"
}

$fixed800 = Join-Path $evidence 'runtime_800_captures'
$fixed1600 = Join-Path $evidence 'runtime_1600_captures'
Run-Gbv 'runtime_800' "--mode=clouds --window=800x450 --gpu-validation --runtime-settings-test=`"$fixed800`" $camera --hold-cloud-time --weather-timeline=overcast:rain_sf --timeline-duration=1.0 --timeline-time=1.15"
Run-Gbv 'runtime_1600' "--mode=clouds --window=1600x900 --gpu-validation --runtime-settings-test=`"$fixed1600`" $camera --hold-cloud-time --weather-timeline=overcast:rain_sf --timeline-duration=1.0 --timeline-time=1.15"

foreach ($mode in @('on', 'off')) {
    Run-Gbv "clouds_$mode" "--mode=clouds --window=800x450 --gpu-validation --clouds=$mode --scene=clouds_rain_horizon --weather-mode=rain_sf $camera --frames=30"
}

Run-Gbv 'transition_debug' "--mode=clouds --window=800x450 --debug --clouds=on $camera --weather-timeline=overcast:rain_sf --timeline-duration=0.5 --timeline-time=0.3 --frames=5"
"gbv matrix complete"
