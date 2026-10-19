param([string]$Executable = '', [int]$Frames = 230)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if (!$Executable) { $Executable = Join-Path $root 'x64/ReleaseDX12/xrDX12Smoke.exe' }
$Executable = (Resolve-Path $Executable).Path
$output = Join-Path $root 'x64/f353b-timings'
New-Item -ItemType Directory -Force $output | Out-Null
if ($Frames -le 30) { throw 'Нужно больше 30 кадров прогрева' }
foreach ($mode in 'legacy','multi','parallel') {
    $workers = if ($mode -eq 'parallel') { 1,2,4,8 } else { 2 }
    foreach ($worker in $workers) {
        $p=Start-Process $Executable -ArgumentList "-commandlists $mode -fixedpose -prepframes $Frames -jobworkers $worker" -WindowStyle Hidden -PassThru
        if (!$p.WaitForExit(60000)) { Stop-Process -Id $p.Id; throw 'Timeout' }
        $log=Get-Content (Join-Path (Split-Path $Executable) 'xrDX12Smoke.log') -Raw
        Set-Content (Join-Path $output "$mode-w$worker.log") $log -Encoding utf8
        if ($p.ExitCode -ne 0 -or !$log.Contains("[recording] PASS mode=$mode") -or
            !$log.Contains("frames=$Frames samples=$($Frames-30)") -or
            !$log.Contains("[recording-gpu] available=1 samples=$($Frames-30)") -or
            $log -match '\[validation\]\[(WARNING|ERROR|CORRUPTION)\]|timestamp Map failed') { throw "Failed: $mode w$worker" }
        Write-Output "$mode workers=$worker"
        [regex]::Matches($log,'\[recording(?:-cpu|-gpu)?\] (?:PASS|scheduling|available)[^\r\n]+') | ForEach-Object { $_.Value }
    }
}
