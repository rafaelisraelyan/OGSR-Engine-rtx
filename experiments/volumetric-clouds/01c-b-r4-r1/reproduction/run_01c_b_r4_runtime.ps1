#requires -Version 5.1
$ErrorActionPreference = 'Stop'

$root = 'C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work'
$exe = Join-Path $root 'build_developer\Release\OGSRCloudsTest.exe'
$evidence = Join-Path $root 'evidence'
New-Item -ItemType Directory -Force -Path $evidence | Out-Null

$camera = '--camera=1000,150,-7000 --camera-yaw=12 --camera-pitch=10 --fov=62 --weather-seed=7331 --time=0 --no-vsync --breakup=off --front-height=on --sun-azimuth=65 --sun-elevation=18 --wind=off'

function Run-Case($name, $argText) {
    $d = Join-Path $evidence $name
    if (Test-Path $d) { Remove-Item $d -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    $proc = Start-Process -FilePath $exe -ArgumentList $argText -NoNewWindow `
        -Wait -PassThru -RedirectStandardOutput "$d\run.log" `
        -RedirectStandardError "$d\run.err.log"
    if ($null -ne $proc.ExitCode -and $proc.ExitCode -ne 0) {
        throw "runtime case failed: $name (exit $($proc.ExitCode))"
    }
    " $name complete"
}

# Fixed weather/time: no reset across quality or clouds toggles.
$fixedDir = Join-Path $evidence 'runtime_fixed'
$fixedArgs = "--mode=clouds --window=800x450 --runtime-settings-test=$fixedDir $camera --hold-cloud-time --weather-timeline=overcast:rain_sf --timeline-duration=1.0 --timeline-time=1.15"
Run-Case 'runtime_fixed' $fixedArgs

# Animated/interrupted timeline: quality/on-off changes do not reset or advance
# weather differently; pause freezes, resume advances.
$animDir = Join-Path $evidence 'runtime_animated'
$animArgs = "--mode=clouds --window=800x450 --runtime-settings-test=$animDir $camera --weather-timeline=overcast:rain_sf --timeline-duration=0.5 --timeline-time=0 --timeline-play --timeline-interrupt=0:0.65:partly"
Run-Case 'runtime_animated' $animArgs

# One 1600x900 runtime session.
$wideDir = Join-Path $evidence 'runtime_fixed_1600'
$wideArgs = "--mode=clouds --window=1600x900 --runtime-settings-test=$wideDir $camera --hold-cloud-time --weather-timeline=overcast:rain_sf --timeline-duration=1.0 --timeline-time=1.15"
Run-Case 'runtime_fixed_1600' $wideArgs

# Matched animated timeline: clouds on vs off, same frame count.
foreach ($mode in @('on', 'off')) {
    $d = Join-Path $evidence "timeline_compare_$mode"
    if (Test-Path $d) { Remove-Item $d -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    $argText = "--mode=clouds --window=800x450 --clouds=$mode $camera --weather-timeline=overcast:rain_sf --timeline-duration=0.5 --timeline-time=0 --timeline-play --timeline-interrupt=0:0.65:partly --fixed-frame-delta=0.0166667 --frames=90 --frame-timing-log=$d\frames.csv"
    $proc = Start-Process -FilePath $exe -ArgumentList $argText -NoNewWindow `
        -Wait -PassThru -RedirectStandardOutput "$d\run.log" `
        -RedirectStandardError "$d\run.err.log"
    if ($null -ne $proc.ExitCode -and $proc.ExitCode -ne 0) {
        throw "timeline compare failed: $mode (exit $($proc.ExitCode))"
    }
    " timeline_compare_$mode complete"
}

foreach ($family in @('runtime_fixed', 'runtime_animated',
                       'runtime_fixed_1600', 'timeline_compare_on',
                       'timeline_compare_off')) {
    $dir = Join-Path $evidence $family
    if (Test-Path $dir) {
        python "D:\Coding Project\OGSR_V_VISUAL_01\tools\bmp_to_png.py" $dir 2>&1 |
            Select-Object -Last 1
    }
}
"runtime evidence complete"

