#requires -Version 5.1
$ErrorActionPreference = 'Stop'

$w = 'C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work'
$dev = Join-Path $w 'build_developer\Release\OGSRCloudsTest.exe'
$ship = Join-Path $w 'build_shipping\Release\OGSRCloudsTest.exe'
$root = Join-Path $w 'evidence\cli'
if (Test-Path $root) { Remove-Item $root -Recurse -Force }
New-Item -ItemType Directory -Force -Path $root | Out-Null
$settings = Join-Path $w 'standalone_01c_b_r4_r1\settings'
$rows = New-Object System.Collections.Generic.List[object]

function Run-Case($exe, $name, $argText, $expected, $expectPattern) {
    $log = Join-Path $root "$name.log"
    $err = Join-Path $root "$name.err.log"
    $proc = Start-Process -FilePath $exe -ArgumentList $argText -NoNewWindow `
        -Wait -PassThru -RedirectStandardOutput $log `
        -RedirectStandardError $err
    $exit = $proc.ExitCode
    $text = if (Test-Path $log) { Get-Content $log -Raw } else { '' }
    $patternOk = $true
    if ($expectPattern) {
        $patternOk = $text -match $expectPattern
    }
    $ok = ($exit -eq $expected) -and $patternOk
    $rows.Add([pscustomobject]@{
        name = $name
        expected_exit = $expected
        actual_exit = $exit
        pattern = $expectPattern
        pattern_ok = $patternOk
        result = if ($ok) { 'PASS' } else { 'FAIL' }
    })
    " $name exit=$exit result=$(if ($ok) {'PASS'} else {'FAIL'})"
}

$developerFlags = @(
    '--weather-mode=rain_sf',
    '--weather-transition=overcast:rain_sf',
    '--transition-phase=0.5',
    '--weather-timeline=clear:partly:overcast',
    '--timeline-duration=1.0',
    '--timeline-time=0.5',
    '--timeline-interrupt=0:0.65:partly',
    '--weather-seed=17',
    '--sun-elevation=18',
    '--sun-azimuth=65',
    '--wind=off',
    '--density=0.5',
    '--coverage=0.5',
    '--storm-front=on',
    '--shadow-od-early-out=0.01',
    '--shadow-inner-only',
    '--p1-matrix',
    '--preset=rain',
    "--capture=$root\neg_capture",
    '--debug-mode=final',
    '--steps=64',
    '--cloud-scale=full',
    '--seed=42',
    '--size=64',
    '--format=unorm',
    '--adapter=0',
    '--warp',
    '--gpu-validation',
    "--frame-timing-log=$root\neg_frames.csv",
    '--hold-cloud-time',
    '--scene=clouds_rain_horizon',
    "--sequence=$root\neg_seq",
    '--fov=60',
    '--camera=0,0,0',
    '--motion=static',
    '--runtime-settings-test=C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work\evidence\neg_runtime_settings',
    '--fixed-frame-delta=0.0166667',
    '--selftest'
)

foreach ($flag in $developerFlags) {
    $safe = ($flag -replace '[^A-Za-z0-9]+', '_').Trim('_')
    Run-Case $ship "ship_neg_$safe" "$flag --help" 2 `
        'does not register developer/harness option'
}

Run-Case $ship 'ship_neg_mode_slices' '--mode=slices --help' 2 `
    'exposes cloud rendering only'
Run-Case $ship 'ship_public' `
    "--mode=clouds --clouds=off --cloud-quality=balanced --cloud-render-mode=volumetric3d --help" `
    0 'Public cloud graphics options'
Run-Case $ship 'ship_help_no_weather_flags' '--help' 0 `
    'Not registered: --seed, --size, --format'
Run-Case $ship 'ship_neg_classic2d' '--cloud-render-mode=classic2d --help' 2 `
    'not implemented in standalone'
Run-Case $ship 'ship_neg_unknown' '--not-a-real-option --help' 2 `
    'does not register developer/harness option'
Run-Case $ship 'ship_neg_fault_injection' "--runtime-settings-fault-test=$root\fault_neg --help" 2 `
    'does not register developer/harness option'
Run-Case $ship 'ship_injected_weather_runtime' "--mode=clouds --window=800x450 --frames=1 --no-vsync --cloud-settings=$(Join-Path $settings 'cloud_settings.injected_weather.json') --clouds=on" 0 'weather controller state: source=CEnvironment-contract-standalone mode=overcast'
Run-Case $ship 'ship_injected_noise_runtime' "--mode=clouds --window=800x450 --frames=1 --no-vsync --cloud-settings=$(Join-Path $settings 'cloud_settings.injected_noise.json')" 0 'noise parameters: 128\^3 seed=1337 octaves=4 baseFreq=4 lacunarity=2 worleyFreq=8 gain=0.5 blend=0.55 erosion=0.35'
Run-Case $ship 'ship_nested_config' "--cloud-settings=$(Join-Path $settings 'cloud_settings.nested_malformed.json') --help" 0 'no valid user values; safe defaults used'
$shipPersist = Join-Path $root 'ship_settings.json'
Run-Case $ship 'ship_save_public' "--mode=clouds --window=800x450 --frames=1 --no-vsync --clouds=off --cloud-quality=performance --save-cloud-settings=$shipPersist" 0 'cloud settings saved:'
Run-Case $ship 'ship_reload_public' `
    "--cloud-settings=$shipPersist --help" 0 'found=1 loaded=1 error=0'

foreach ($flag in $developerFlags) {
    if ($flag -eq '--selftest') { continue }
    $safe = ($flag -replace '[^A-Za-z0-9]+', '_').Trim('_')
    Run-Case $dev "dev_accept_$safe" "$flag --help" 0 `
        'standalone D3D12 volumetric clouds'
}
Run-Case $dev 'dev_accept_mode_slices' '--mode=slices --help' 0 `
    'V0 noise slice viewer'

Run-Case $dev 'dev_neg_classic2d' '--cloud-render-mode=classic2d --help' 2 `
    'not implemented in standalone'
Run-Case $dev 'dev_neg_fault_injection' "--runtime-settings-fault-test=$root\fault_neg --help" 2 `
    'unknown argument'
Run-Case $dev 'dev_neg_clouds' '--clouds=maybe --help' 2 `
    'invalid --clouds value'
Run-Case $dev 'dev_neg_quality_conflict' `
    '--cloud-quality=performance --profile=reference --help' 2 `
    'conflicting --cloud-quality/--profile values'
Run-Case $dev 'dev_valid_config' `
    "--cloud-settings=$(Join-Path $settings 'cloud_settings.valid.json') --help" `
    0 'found=1 loaded=1 error=0'
Run-Case $dev 'dev_partial_config' `
    "--cloud-settings=$(Join-Path $settings 'cloud_settings.partial.json') --help" `
    0 'found=1 loaded=1 error=0'
Run-Case $dev 'dev_corrupt_config' `
    "--cloud-settings=$(Join-Path $settings 'cloud_settings.corrupt.json') --help" `
    0 'no valid user values; safe defaults used'
Run-Case $dev 'dev_unknown_version' `
    "--cloud-settings=$(Join-Path $settings 'cloud_settings.unknown_future.json') --help" `
    0 'unsupported cloud settings schema_version'
Run-Case $dev 'dev_classic2d_config' `
    "--cloud-settings=$(Join-Path $settings 'cloud_settings.classic2d_fallback.json') --help" `
    0 'standalone falls back to volumetric3d'
Run-Case $dev 'dev_injected_config' `
    "--cloud-settings=$(Join-Path $settings 'cloud_settings.injected_weather.json') --help" `
    0 'weather/debug/noise keys are never applied'
Run-Case $dev 'dev_injected_noise_config' `
    "--cloud-settings=$(Join-Path $settings 'cloud_settings.injected_noise.json') --help" `
    0 'weather/debug/noise keys are never applied'
Run-Case $dev 'dev_nested_malformed_config' `
    "--cloud-settings=$(Join-Path $settings 'cloud_settings.nested_malformed.json') --help" `
    0 'no valid user values; safe defaults used'
Run-Case $dev 'dev_noise_runtime' "--mode=clouds --window=800x450 --frames=1 --no-vsync --seed=42 --size=64 --format=unorm" 0 'noise parameters: 64\^3 seed=42'
Run-Case $dev 'dev_weather_runtime' "--mode=clouds --window=800x450 --frames=1 --no-vsync --weather-timeline=overcast:rain_sf --timeline-duration=1.0 --timeline-time=1.15 --hold-cloud-time --time=0 --shadow-od-early-out=0.01" 0 'weather timeline: overcast -> rain_sf'

$persistDir = Join-Path $root 'persist'
New-Item -ItemType Directory -Force -Path $persistDir | Out-Null
$persist = Join-Path $persistDir 'cloud_settings.json'
$hashes = @()
for ($i = 1; $i -le 3; ++$i) {
    Run-Case $dev "persist_save_$i" "--mode=clouds --window=800x450 --frames=1 --no-vsync --clouds=off --cloud-quality=balanced --cloud-settings=$persist --save-cloud-settings=$persist" 0 'cloud settings saved:'
    $hashes += (Get-FileHash $persist -Algorithm SHA256).Hash.ToLower()
}
$rows.Add([pscustomobject]@{
    name = 'persist_repeat_identity'
    expected_exit = 'same hash'
    actual_exit = ($hashes | Select-Object -Unique).Count
    pattern = '3 saves produce one hash'
    pattern_ok = (($hashes | Select-Object -Unique).Count -eq 1)
    result = if (($hashes | Select-Object -Unique).Count -eq 1) { 'PASS' } else { 'FAIL' }
})
Run-Case $dev 'persist_reload' "--mode=clouds --window=800x450 --frames=1 --no-vsync --cloud-settings=$persist --cloud-quality=reference" 0 'cloud graphics settings: enabled=0 render_mode=volumetric3d quality=reference'
Run-Case $dev 'persist_reload_no_override' "--mode=clouds --window=800x450 --frames=1 --no-vsync --cloud-settings=$persist" 0 'cloud graphics settings: enabled=0 render_mode=volumetric3d quality=balanced'
Remove-Item $persist -Force
Run-Case $dev 'factory_reset_missing' `
    "--cloud-settings=$persist --help" 0 'found=0 loaded=0 error=0'

$rows | Export-Csv (Join-Path $root 'cli_tests.csv') -NoTypeInformation
$failed = @($rows | Where-Object { $_.result -ne 'PASS' })
"cases=$($rows.Count) failed=$($failed.Count)"
if ($failed.Count -gt 0) {
    $failed | ForEach-Object { "FAIL: $($_.name)" }
    exit 1
}
exit 0




