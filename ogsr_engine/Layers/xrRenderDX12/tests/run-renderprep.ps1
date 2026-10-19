param([string]$Executable = '', [int]$Frames = 230)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if (!$Executable) { $Executable = Join-Path $root 'x64/ReleaseDX12/xrDX12Smoke.exe' }
$Executable = (Resolve-Path $Executable).Path
$output = Join-Path $root 'x64/f330-results'
New-Item -ItemType Directory -Force $output | Out-Null
$results = [Collections.Generic.List[object]]::new()
$script:imageHash = ''
function Run([string]$Name, [string]$Arguments, [string]$Expected, [bool]$Gpu = $true) {
    $process = Start-Process $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(45000)) { Stop-Process -Id $process.Id; throw "Timeout: $Name" }
    $log = Get-Content (Join-Path (Split-Path $Executable) 'xrDX12Smoke.log') -Raw
    Set-Content (Join-Path $output "$Name.log") $log -Encoding utf8
    $pass = $process.ExitCode -eq 0 -and $log.Contains($Expected) -and
        $log -notmatch '\[validation\]\[(WARNING|ERROR|CORRUPTION)\]|\[renderprep\] FAIL'
    if ($Gpu) {
        $pass = $pass -and $log.Contains('[renderprep] PASS') -and
            $log.Contains('GPU drained before shutdown') -and $log.Contains('72 vertices (2 cubes)')
        if ($Arguments.Contains('-renderprep parallel') -or $Arguments.Contains('-renderprep verify')) {
            $pass = $pass -and $log.Contains('starts=1') -and $log.Contains('participants=0')
        }
    }
    if ($Name.StartsWith('depth-')) {
        $hash = [regex]::Match($log,'\[depth-frame\] hash=([0-9a-f]+)').Groups[1].Value
        $pass = $pass -and $hash.Length -eq 16
        if (!$script:imageHash) { $script:imageHash = $hash }
        $pass = $pass -and $hash -eq $script:imageHash
    }
    if ($Name.StartsWith('resize-')) {
        # F3.3.0a: третий resize намеренно НЕ 16:9 (1024x768). Прежние два
        # размера сохраняли соотношение сторон, поэтому смена аспекта камеры
        # после resize не проверялась ни одним прогоном.
        $pass = $pass -and $log.Contains('Resized to 1024x768')
        $pass = $pass -and $log.Contains('resizeSuccess=3')
    }
    if ($Name.StartsWith('bench-')) {
        $pass = $pass -and $log.Contains("frames=$Frames samples=$($Frames-30)")
        if ($Name -eq 'bench-serial') { $pass = $pass -and !$log.Contains('[jobs] ready') }
    }
    $summary = [regex]::Match($log,'\[renderprep\] PASS[^\r\n]+').Value
    $results.Add([pscustomobject]@{name=$Name; arguments=$Arguments; pass=$pass; exit=$process.ExitCode; summary=$summary})
    $results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    Write-Output "$Name pass=$pass exit=$($process.ExitCode) $summary"
    if (!$pass) { throw "Failed: $Name" }
}
if ($Frames -le 30) { throw 'Frames должен превышать 30 кадров прогрева' }
Run 'prep-selftest' '-prepselftest' '[prep-selftest] PASS' $false
Run 'cpu-selftest' '-cpuselftest' '[cpu-selftest] PASS' $false
foreach ($mode in 'serial','parallel','verify') {
    Run "depth-$mode" "-depthtest -renderprep $mode -jobworkers 4" '[smoke] -depthtest: PASS'
    Run "resize-$mode" "-renderprep $mode -jobworkers 4 -resizetest -cputest 60" '[cputest] PASS'
}
Run 'bench-serial' "-renderprep serial -fixedpose -prepframes $Frames" '[renderprep] PASS'
foreach ($workers in 1,2,4,8) {
    Run "bench-parallel-w$workers" "-renderprep parallel -jobworkers $workers -fixedpose -prepframes $Frames" '[renderprep] PASS'
    Run "bench-verify-w$workers" "-renderprep verify -jobworkers $workers -fixedpose -prepframes $Frames" '[renderprep] PASS'
}

# F3.3.0a: WM_TIMER / modal sizing path. run-renderprep.ps1 не отправлял
# WM_ENTERSIZEMOVE/WM_TIMER, поэтому этот путь не имел автоматического
# покрытия; тест подтверждает, что кадры из WM_TIMER действительно
# отрисованы, а не только что сообщения доставлены.
foreach ($mode in 'parallel','verify') {
    $timerArgs = @{ Executable = $Executable; Mode = $mode }
    $timerOut = & (Join-Path $PSScriptRoot 'run-wmtimer.ps1') @timerArgs
    $timerOut | Write-Output
    $timerPass = ($LASTEXITCODE -eq 0)
    if (!$timerPass) { throw "Failed: wmtimer-$mode" }

    # F3.3.0a: same path with the test-only one-shot WM_TIMER fault.
    $faultArgs = @{ Executable = $Executable; Mode = $mode; TimerFault = $true }
    $faultOut = & (Join-Path $PSScriptRoot 'run-wmtimer.ps1') @faultArgs
    $faultOut | Write-Output
    $faultPass = ($LASTEXITCODE -eq 0)
    if (!$faultPass) { throw "Failed: wmtimer-fault-$mode" }
}
