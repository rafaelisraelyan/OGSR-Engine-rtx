param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path $Executable).Path
$output = Join-Path (Split-Path $Executable) 'overlap-results'
New-Item -ItemType Directory -Force $output | Out-Null
$summary = @()
foreach ($workers in 1,2,4,8) {
    # Только бинарник с GR_RECORDING_TRACE; никаких -recordfault/rendezvous.
    $p = Start-Process $Executable -ArgumentList "-commandlists parallel -lists 2 -draws 2048 -fixedpose -prepframes 60 -jobworkers $workers -nodebug -nogbv" -WindowStyle Hidden -PassThru
    if (!$p.WaitForExit(60000)) { Stop-Process -Id $p.Id; throw 'Overlap run timeout' }
    $log = Get-Content (Join-Path (Split-Path $Executable) 'xrDX12Smoke.log') -Raw
    Set-Content (Join-Path $output "workers-$workers.log") $log -Encoding utf8
    $intervals = [regex]::Matches($log, '\[recording-natural\] callbacks=2 pairs=1 overlapping=(\d+) overlapUs=([0-9.]+)')
    if ($p.ExitCode -ne 0 -or $intervals.Count -ne 60 -or $log.Contains('[parallel-fault]') -or
        $log.Contains('ABORTED') -or !$log.Contains('[recording] PASS mode=parallel')) {
        throw "Invalid trace run workers=$workers; use a GR_RECORDING_TRACE build"
    }
    $measured = @($intervals | Select-Object -Skip 30)
    $overlapped = @($measured | Where-Object { $_.Groups[1].Value -eq '1' }).Count
    $us = @($measured | ForEach-Object {
        [double]::Parse($_.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
    })
    $summary += [pscustomobject]@{
        Workers=$workers; Frames=$measured.Count; Overlapped=$overlapped
        MeanOverlapUs=($us | Measure-Object -Average).Average
        MaxOverlapUs=($us | Measure-Object -Maximum).Maximum
    }
    Write-Output "workers=$workers naturalOverlap=$overlapped/$($measured.Count)"
}
$summary | ConvertTo-Json | Set-Content (Join-Path $output 'summary.json') -Encoding utf8
# Нулевой overlap допустим: это результат наблюдения, а не ошибка renderer.
