param([string]$Executable = '', [int]$Frames = 230)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if (!$Executable) { $Executable = Join-Path $root 'x64/ReleaseDX12/xrDX12Smoke.exe' }
$Executable = (Resolve-Path $Executable).Path
$output = Join-Path $root 'x64/f353a-results'
New-Item -ItemType Directory -Force $output | Out-Null
$results = [Collections.Generic.List[object]]::new()
if ($Frames -le 30) { throw 'Нужно больше 30 кадров прогрева' }
function Run([string]$Name, [string]$Arguments, [string]$Mode, [bool]$Fault = $false) {
    $p = Start-Process $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (!$p.WaitForExit(60000)) { Stop-Process -Id $p.Id; throw "Timeout: $Name" }
    $log = Get-Content (Join-Path (Split-Path $Executable) 'xrDX12Smoke.log') -Raw
    Set-Content (Join-Path $output "$Name.log") $log -Encoding utf8
    $lists = if ($Mode -eq 'multi') { 4 } else { 1 }
    $pass = $p.ExitCode -eq 0 -and $log.Contains("[recording] PASS mode=$Mode lists=$lists") -and
        $log.Contains('GPU drained before shutdown') -and $log.Contains('72 vertices (2 cubes)') -and
        $log.Contains('GPU-based validation=1') -and
        $log -notmatch '\[validation\]\[(WARNING|ERROR|CORRUPTION)\]|\[renderprep\] FAIL|\[recording-recovery\] FAIL'
    if ($Arguments.Contains('-depthtest')) {
        $pass = $pass -and $log.Contains('[depth-frame] hash=9dd37d87b8eb202d') -and $log.Contains('[smoke] -depthtest: PASS')
    }
    if ($Arguments.Contains('-resizetest')) {
        $pass = $pass -and $log.Contains('resizeSuccess=3') -and $log.Contains('Resized to 1024x768')
    }
    if ($Name.StartsWith('bench-')) {
        $pass = $pass -and $log.Contains("frames=$Frames samples=$($Frames-30)") -and
            ([regex]::Matches($log,'\[recording\] resources').Count -eq 1)
    }
    if ($Fault) {
        $pass = $pass -and $log.Contains('[recording-recovery] PASS') -and $log.Contains('submittedUnchanged=1') -and
            $log.Contains('submitted=1') -and $log.Contains('[recording] ABORTED mode=multi lists=4 frames=1') -and
            ([regex]::Matches($log,'GPU drained before shutdown').Count -eq 2)
    } else { $pass = $pass -and !$log.Contains('[recording] ABORTED') }
    $summary = [regex]::Match($log,'\[recording\] PASS[^\r\n]+').Value
    $results.Add([pscustomobject]@{name=$Name; arguments=$Arguments; pass=$pass; summary=$summary})
    $results | ConvertTo-Json | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    Write-Output "$Name pass=$pass $summary"
    if (!$pass) { throw "Failed: $Name" }
}
Run 'default' '-depthtest' 'legacy'
foreach ($mode in 'legacy','multi') {
    foreach ($prep in 'serial','parallel','verify') {
        Run "depth-$mode-$prep" "-depthtest -commandlists $mode -renderprep $prep -jobworkers 4" $mode
        Run "resize-$mode-$prep" "-commandlists $mode -renderprep $prep -jobworkers 4 -resizetest -cputest 60" $mode
    }
    Run "bench-$mode" "-commandlists $mode -fixedpose -prepframes $Frames" $mode
}
foreach ($stage in 'allocator','reset','record','close') {
    foreach ($list in 0..3) {
        Run "fault-$stage-$list" "-depthtest -commandlists multi -recordfault $stage $list" 'multi' $true
    }
}
foreach ($mode in 'parallel','verify') {
    & (Join-Path $PSScriptRoot 'run-wmtimer.ps1') -Executable $Executable -Mode $mode -CommandLists multi
    if ($LASTEXITCODE -ne 0) { throw "WM_TIMER $mode" }
    Copy-Item (Join-Path (Split-Path $Executable) 'xrDX12Smoke.log') (Join-Path $output "timer-$mode.log")
    & (Join-Path $PSScriptRoot 'run-wmtimer.ps1') -Executable $Executable -Mode $mode -CommandLists multi -TimerFault
    if ($LASTEXITCODE -ne 0) { throw "WM_TIMER fault $mode" }
    Copy-Item (Join-Path (Split-Path $Executable) 'xrDX12Smoke.log') (Join-Path $output "timer-fault-$mode.log")
}
