#requires -Version 5.1
$ErrorActionPreference = 'Stop'

$w = 'C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work'
$exe = Join-Path $w 'build_developer\Release\OGSRCloudsTest.exe'
$root = Join-Path $w 'evidence\static'
if (Test-Path $root) { Remove-Item $root -Recurse -Force }
New-Item -ItemType Directory -Force -Path $root | Out-Null

$scenes = @{
    clear = 'clouds_clear_horizon'
    partly = 'clouds_partly_horizon'
    overcast = 'clouds_overcast_horizon'
    rain_sf = 'clouds_rain_horizon'
}
$views = @(
    @{ n='front';   c='0,500,-9000';    y='12'; p='8' },
    @{ n='ground';  c='1000,150,-7000'; y='12'; p='10' },
    @{ n='oblique'; c='-3000,700,0';    y='55'; p='8' }
)

function Run-Capture($out, $argText, $label) {
    if (Test-Path $out) { Remove-Item $out -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $proc = Start-Process -FilePath $exe -ArgumentList $argText -NoNewWindow `
        -Wait -PassThru -RedirectStandardOutput "$out\run.log" `
        -RedirectStandardError "$out\run.err.log"
    if ($null -ne $proc.ExitCode -and $proc.ExitCode -ne 0) {
        throw "static capture failed: $label (exit $($proc.ExitCode))"
    }
    " $label"
}

foreach ($mode in @('clear', 'partly', 'overcast', 'rain_sf')) {
    $scene = $scenes[$mode]
    foreach ($view in $views) {
        $d = Join-Path $root "static_800\$mode\$($view.n)\p1"
        $argText = "--mode=clouds --scene=$scene --weather-mode=$mode " +
            "--capture=$d --window=800x450 --cloud-scale=full " +
            "--camera=$($view.c) --camera-yaw=$($view.y) " +
            "--camera-pitch=$($view.p) --fov=60 --steps=128 " +
            "--shadow-steps=6 --weather-seed=7331 --time=0 --no-vsync " +
            "--breakup=off --front-height=on --sun-azimuth=65 " +
            "--sun-elevation=18 --wind=off"
        Run-Capture $d $argText "static_800/$mode/$($view.n)"
    }
    $d = Join-Path $root "static_1600\$mode\ground\p1"
    $argText = "--mode=clouds --scene=$scene --weather-mode=$mode " +
        "--capture=$d --window=1600x900 --cloud-scale=full " +
        "--camera=1000,150,-7000 --camera-yaw=12 --camera-pitch=10 " +
        "--fov=60 --steps=128 --shadow-steps=6 --weather-seed=7331 " +
        "--time=0 --no-vsync --breakup=off --front-height=on " +
        "--sun-azimuth=65 --sun-elevation=18 --wind=off"
    Run-Capture $d $argText "static_1600/$mode/ground"
}

python "D:\Coding Project\OGSR_V_VISUAL_01\tools\bmp_to_png.py" $root 2>&1 |
    Select-Object -Last 1
"static capture matrix complete"

