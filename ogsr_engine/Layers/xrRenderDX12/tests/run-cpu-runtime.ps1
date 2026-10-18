param([int]$Frames = 200)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$exe = Join-Path $root 'x64/ReleaseDX12/xrDX12Smoke.exe'
$sourceLog = Join-Path (Split-Path $exe) 'xrDX12Smoke.log'
$output = Join-Path $root 'x64/f32c2'
New-Item -ItemType Directory -Force $output | Out-Null
$results = [Collections.Generic.List[object]]::new()
function Run-Smoke([string]$Name, [string]$Arguments, [string]$Expected) {
    $process = Start-Process -FilePath $exe -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    $timedOut = !$process.WaitForExit(45000)
    if ($timedOut) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
    $log = Get-Content -LiteralPath $sourceLog -Raw
    Copy-Item -LiteralPath $sourceLog -Destination (Join-Path $output "$Name.log")
    $passed = !$timedOut -and $process.ExitCode -eq 0 -and $log.Contains($Expected) -and
        $log -notmatch '\[validation\]\[(ERROR|CORRUPTION|WARNING)\]'
    if ($Expected -ne '[cpu-selftest] PASS') {
        $passed = $passed -and $log.Contains('GPU drained before shutdown') -and
            $log.Contains('72 vertices (2 cubes)')
    }
    $metrics = @{}
    foreach ($match in [regex]::Matches($log, '\[cpu-metric\] ([\w-]+) mean=([\d.]+) p50=([\d.]+) p95=([\d.]+) p99=([\d.]+)')) {
        $metrics[$match.Groups[1].Value] = @{
            mean = [double]$match.Groups[2].Value; p50 = [double]$match.Groups[3].Value
            p95 = [double]$match.Groups[4].Value; p99 = [double]$match.Groups[5].Value
        }
    }
    $results.Add([pscustomobject]@{name=$Name; arguments=$Arguments; passed=$passed;
        exitCode=$process.ExitCode; timedOut=$timedOut; metrics=$metrics})
    $results | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 (Join-Path $output 'runtime-results.json')
    Write-Output "$Name passed=$passed exit=$($process.ExitCode)"
    if (!$passed) { throw "Проверка $Name не прошла; см. $output/$Name.log" }
}
Run-Smoke 'cpu-selftest' '-cpuselftest' '[cpu-selftest] PASS'
Run-Smoke 'depth-baseline' '-depthtest' '[smoke] -depthtest: PASS'
Run-Smoke 'depth-parallel' '-depthtest -jobs parallel -stress 1024 -jobsverify' '[smoke] -depthtest: PASS'
Run-Smoke 'resize-parallel' '-jobs parallel -stress 4096 -cputest 60 -resizetest' '[cputest] PASS'
foreach ($count in 256,1024,4096,16384) {
    foreach ($workers in 1,2,4,8) {
        Run-Smoke "verify-$count-w$workers" "-jobs parallel -stress $count -jobworkers $workers -cputest $Frames" '[cputest] PASS'
    }
    # Режим serial с проверкой всё равно обязан реально выполнить parallel.
    Run-Smoke "verify-serial-$count" "-jobs serial -stress $count -jobworkers 4 -cputest 30" '[cputest] PASS'
    Run-Smoke "bench-serial-$count" "-jobs serial -stress $count -cpubench $Frames" '[cputest] PASS'
    Run-Smoke "bench-parallel-$count" "-jobs parallel -stress $count -jobworkers 4 -cpubench $Frames" '[cputest] PASS'
}
