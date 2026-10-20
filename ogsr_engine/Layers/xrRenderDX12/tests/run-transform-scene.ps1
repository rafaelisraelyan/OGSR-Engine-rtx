param([string]$Executable = '')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if (!$Executable) { $Executable = Join-Path $root 'x64/ReleaseDX12/xrDX12Smoke.exe' }
$Executable = (Resolve-Path $Executable).Path
$output = Join-Path $root 'x64/f356a-results'
New-Item -ItemType Directory -Force $output | Out-Null
$results = [Collections.Generic.List[object]]::new()
$expectedHash = '[transform-frame] hash=c13dd65927d9727d'

function Run([string]$Name, [string]$Arguments) {
    $logPath = Join-Path (Split-Path $Executable) 'xrDX12Smoke.log'
    Remove-Item $logPath -ErrorAction SilentlyContinue
    $p = Start-Process $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (!$p.WaitForExit(120000)) { Stop-Process -Id $p.Id -Force; throw "Timeout: $Name" }
    $log = Get-Content $logPath -Raw
    Set-Content (Join-Path $output "$Name.log") $log -Encoding utf8
    return [pscustomobject]@{ Name=$Name; Exit=$p.ExitCode; Log=$log; Arguments=$Arguments }
}

function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}

function NoValidation([string]$Log) {
    return $Log -notmatch '\[validation\]\[(WARNING|ERROR|CORRUPTION)\]'
}

function HasHash([string]$Log) {
    return $Log.Contains($expectedHash)
}

function TransformPass([string]$Log) {
    return $Log.Contains('[smoke] -transformtest: PASS') -and
        $Log.Contains('[transform-frame] PASS')
}

function PacketCounts([string]$Log, [int]$Visible, [int]$Draws) {
    return $Log.Contains("[packet-draw] visible=$Visible draws=$Draws")
}

function JobsClean([string]$Log) {
    $m = [regex]::Match($Log, '\[jobs\] stopped: starts=(\d+) submitted=(\d+) completed=(\d+) failures=(\d+) unobserved=(\d+) participants=(\d+)')
    return $m.Success -and $m.Groups[1].Value -eq '1' -and
        $m.Groups[2].Value -eq $m.Groups[3].Value -and
        $m.Groups[4].Value -eq '0' -and $m.Groups[5].Value -eq '0' -and
        $m.Groups[6].Value -eq '0'
}

function Record([object]$Run) {
    $results.Add([pscustomobject]@{ name=$Run.Name; pass=$true; exit=$Run.Exit; arguments=$Run.Arguments })
    $results | ConvertTo-Json | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    Write-Output "$($Run.Name) pass=True"
}

# L=1 owner path: two objects, same local geometry, distinct World matrices.
$r = Run 'l1-one-frame' '-scene transforms -drawsource packets -transformtest'
Assert ($r.Exit -eq 0) 'l1: exit'
Assert (HasHash $r.Log) 'l1: hash'
Assert (TransformPass $r.Log) 'l1: oracle'
Assert (PacketCounts $r.Log 2 2) 'l1: packet counts'
Assert $r.Log.Contains('[recording] PASS mode=packets lists=1 frames=1') 'l1: recording'
Assert (NoValidation $r.Log) 'l1: validation'
Record $r

# Repeated frames rotate through all frame slots and object CB regions.
$r = Run 'l1-eight-frames' '-scene transforms -drawsource packets -transformtest -transformframes 8'
Assert ($r.Exit -eq 0) 'l1-8: exit'
Assert (HasHash $r.Log) 'l1-8: hash'
Assert (TransformPass $r.Log) 'l1-8: oracle'
Assert $r.Log.Contains('[recording] PASS mode=packets lists=1 frames=8') 'l1-8: eight frames'
Assert (NoValidation $r.Log) 'l1-8: validation'
Record $r

$r = Run 'l2-multi-eight-frames' '-scene transforms -drawsource packets -commandlists multi -packetlists 2 -transformtest -transformframes 8'
Assert ($r.Exit -eq 0) 'l2-multi: exit'
Assert (HasHash $r.Log) 'l2-multi: hash'
Assert (TransformPass $r.Log) 'l2-multi: oracle'
Assert (PacketCounts $r.Log 2 2) 'l2-multi: packet counts'
Assert $r.Log.Contains('[recording] PASS mode=packets lists=4 frames=8') 'l2-multi: recording'
Assert (NoValidation $r.Log) 'l2-multi: validation'
Record $r

foreach ($workers in 1,2,4) {
    $r = Run "l2-parallel-w$workers-eight-frames" "-scene transforms -drawsource packets -commandlists parallel -packetlists 2 -jobworkers $workers -transformtest -transformframes 8"
    Assert ($r.Exit -eq 0) "l2-w${workers}: exit"
    Assert (HasHash $r.Log) "l2-w${workers}: hash"
    Assert (TransformPass $r.Log) "l2-w${workers}: oracle"
    Assert (PacketCounts $r.Log 2 2) "l2-w${workers}: packet counts"
    Assert $r.Log.Contains("[packet-recording] parallel=1 tasks=2 backgroundWorkers=$workers") "l2-w${workers}: tasks"
    Assert (JobsClean $r.Log) "l2-w${workers}: jobs"
    Assert $r.Log.Contains('[recording] PASS mode=packets lists=4 frames=8') "l2-w${workers}: recording"
    Assert (NoValidation $r.Log) "l2-w${workers}: validation"
    Record $r
}

# Empty transform frame: clear only, object constants are not written.
$r = Run 'empty-background' '-scene transforms -drawsource packets -packetfault hideall -backgroundtest'
Assert ($r.Exit -eq 0) 'empty: exit'
Assert $r.Log.Contains('[background-frame] PASS') 'empty: background oracle'
Assert (PacketCounts $r.Log 0 0) 'empty: packet counts'
Assert (NoValidation $r.Log) 'empty: validation'
Record $r

# Resize/recreate keeps the transform scene and object CB upload alive.
$r = Run 'resize-transform-scene' '-scene transforms -drawsource packets -commandlists parallel -packetlists 2 -jobworkers 2 -resizetest -cputest 60'
Assert ($r.Exit -eq 0) 'resize: exit'
Assert ($r.Log -match 'resizeSuccess=([3-9]|\d{2,})') 'resize: at least three successful resizes'
Assert (PacketCounts $r.Log 2 2) 'resize: packet counts'
Assert (NoValidation $r.Log) 'resize: validation'
Record $r

# Shutdown -> Initialize -> frame must recreate transform mesh and PSObject.
$r = Run 'reinit-transform-scene' '-scene transforms -drawsource packets -commandlists parallel -packetlists 2 -jobworkers 2 -prepfault'
Assert ($r.Exit -eq 0) 'reinit: exit'
Assert $r.Log.Contains('[prep-recovery] PASS') 'reinit: recovery marker'
Assert (PacketCounts $r.Log 2 2) 'reinit: packet counts'
Assert (NoValidation $r.Log) 'reinit: validation'
Record $r

# Invalid (singular affine) World must be rejected before Reset/Execute.
$r = Run 'reject-invalid-transform' '-scene transforms -drawsource packets -packetfault invalidtransform -prepframes 2'
Assert ($r.Exit -ne 0) 'invalid-transform: expected failure'
Assert ($r.Log -match '\[packet-draw\] FAIL') 'invalid-transform: explicit failure'
Assert ($r.Log -match '\[recording\] (PASS|ABORTED) mode=packets lists=1 frames=0') 'invalid-transform: no Execute'
Assert (NoValidation $r.Log) 'invalid-transform: validation'
Record $r

Write-Output ("TRANSFORM SCENE PASS: " + $results.Count + " cases")
