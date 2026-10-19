param([string]$Executable = '')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if (!$Executable) { $Executable = Join-Path $root 'x64/ReleaseDX12/xrDX12Smoke.exe' }
$Executable = (Resolve-Path $Executable).Path
$output = Join-Path $root 'x64/f353b-results'
New-Item -ItemType Directory -Force $output | Out-Null
$results = [Collections.Generic.List[object]]::new()
function Run([string]$Name, [string]$Arguments, [int]$Stage = -1, [int]$List = -1) {
    $p = Start-Process $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (!$p.WaitForExit(60000)) { Stop-Process -Id $p.Id; throw "Timeout: $Name" }
    $log = Get-Content (Join-Path (Split-Path $Executable) 'xrDX12Smoke.log') -Raw
    Set-Content (Join-Path $output "$Name.log") $log -Encoding utf8
    $pass = $p.ExitCode -eq 0 -and $log.Contains('[recording] PASS mode=parallel lists=4') -and
        $log.Contains('GPU drained before shutdown') -and $log.Contains('GPU-based validation=1') -and
        $log -notmatch '\[validation\]\[(WARNING|ERROR|CORRUPTION)\]|\[renderprep\] FAIL|\[recording-recovery\] FAIL'
    $jobs = [regex]::Match($log,'\[jobs\] stopped: starts=(\d+) submitted=(\d+) completed=(\d+) failures=(\d+) unobserved=(\d+) participants=(\d+)')
    $pass = $pass -and $jobs.Success -and $jobs.Groups[1].Value -eq '1' -and
        $jobs.Groups[2].Value -eq $jobs.Groups[3].Value -and $jobs.Groups[5].Value -eq '0' -and $jobs.Groups[6].Value -eq '0'
    if ($Arguments.Contains('-depthtest')) { $pass = $pass -and $log.Contains('[depth-frame] hash=9dd37d87b8eb202d') }
    if ($Arguments.Contains('-resizetest')) { $pass = $pass -and $log.Contains('resizeSuccess=3') -and $log.Contains('Resized to 1024x768') }
    if ($Stage -ge 0) {
        $pass = $pass -and $log.Contains("[recording-recovery] PASS stage=$Stage list=$List submittedUnchanged=1") -and
            $log.Contains('[recording] ABORTED mode=parallel lists=4 frames=1') -and
            ([regex]::Matches($log,'GPU drained before shutdown').Count -eq 2)
        if ($List -in 1,2) {
            $marker = if ($Stage -eq 4) { 'entered=1 exited=1 overlap=0' } else { 'entered=2 exited=2 overlap=1' }
            $pass = $pass -and $log.Contains("[parallel-fault] stage=$Stage list=$List $marker submitted=1") -and $log.Contains('ABORTED drained=1')
        }
        $expectedFailures = if ($Stage -eq 5) { '1' } else { '0' }
        $pass = $pass -and $jobs.Groups[4].Value -eq $expectedFailures
    } else { $pass = $pass -and !$log.Contains('ABORTED') -and $jobs.Groups[4].Value -eq '0' }
    $results.Add([pscustomobject]@{name=$Name; pass=$pass; arguments=$Arguments})
    $results | ConvertTo-Json | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    Write-Output "$Name pass=$pass"
    if (!$pass) { throw "Failed: $Name" }
}
foreach ($workers in 1,2,4,8) {
    foreach ($prep in 'serial','parallel','verify') {
        Run "depth-$prep-w$workers" "-commandlists parallel -depthtest -renderprep $prep -jobworkers $workers"
    }
}
foreach ($prep in 'serial','parallel','verify') {
    Run "resize-$prep" "-commandlists parallel -renderprep $prep -jobworkers 2 -resizetest -cputest 60"
}
$stage=0
foreach ($fault in 'allocator','reset','record','close','submit','throw') {
    $lists = if ($stage -lt 4) { 0..3 } else { 1..2 }
    foreach ($list in $lists) {
        Run "fault-$fault-$list" "-commandlists parallel -depthtest -jobworkers 2 -recordfault $fault $list" $stage $list
    }
    ++$stage
}
# Один background worker + помогающий owner: rendezvous также обязан завершиться.
Run 'fault-one-worker' '-commandlists parallel -depthtest -jobworkers 1 -recordfault throw 2' 5 2
foreach ($mode in 'parallel','verify') {
    foreach ($fault in $false,$true) {
        & (Join-Path $PSScriptRoot 'run-wmtimer.ps1') -Executable $Executable -Mode $mode -CommandLists parallel -TimerFault:$fault
        if ($LASTEXITCODE -ne 0) { throw "WM_TIMER $mode fault=$fault" }
        Copy-Item (Join-Path (Split-Path $Executable) 'xrDX12Smoke.log') (Join-Path $output "timer-$mode-$fault.log")
    }
}
