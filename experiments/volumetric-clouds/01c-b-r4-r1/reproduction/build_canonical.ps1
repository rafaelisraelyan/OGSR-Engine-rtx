#requires -Version 5.1
# Build the three Windows Release configurations of the R4-R1 candidate.
$ErrorActionPreference = 'Stop'
$root = 'C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work'
$src = Join-Path $root 'standalone_01c_b_r4_r1'

function Build-Config([string]$name, [string]$tag, [string[]]$flags) {
    $dir = Join-Path $root $name
    Write-Host "=== configure $name ==="
    & cmake -S $src -B $dir -G "Visual Studio 17 2022" -A x64 @flags `
        *> (Join-Path $root "cmake_$tag.log")
    if ($LASTEXITCODE -ne 0) { throw "configure failed: $name" }
    Write-Host "=== build $name ==="
    & cmake --build $dir --config Release -- /m `
        *> (Join-Path $root "build_$tag.log")
    if ($LASTEXITCODE -ne 0) { throw "build failed: $name" }
    Write-Host "=== ctest $name ==="
    & ctest --test-dir $dir -C Release --output-on-failure `
        *> (Join-Path $root "ctest_$tag.log")
    if ($LASTEXITCODE -ne 0) { throw "ctest failed: $name" }
}

Build-Config 'build_fault' 'fault' @(
    '-DOGSR_WEATHER_DEVELOPER_TOOLS=ON',
    '-DOGSR_WEATHER_SHIPPING_CONTROLS=OFF',
    '-DOGSR_CLOUDS_FAULT_INJECTION=ON')
Build-Config 'build_developer' 'developer' @(
    '-DOGSR_WEATHER_DEVELOPER_TOOLS=ON',
    '-DOGSR_WEATHER_SHIPPING_CONTROLS=OFF',
    '-DOGSR_CLOUDS_FAULT_INJECTION=OFF')
Build-Config 'build_shipping' 'shipping' @(
    '-DOGSR_WEATHER_DEVELOPER_TOOLS=OFF',
    '-DOGSR_WEATHER_SHIPPING_CONTROLS=ON',
    '-DOGSR_CLOUDS_FAULT_INJECTION=OFF')

Write-Host 'R4-R1 BUILD COMPLETE'
