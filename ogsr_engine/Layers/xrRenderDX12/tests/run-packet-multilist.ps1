param([string]$Executable = '')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if (!$Executable) { $Executable = Join-Path $root 'x64/ReleaseDX12/xrDX12Smoke.exe' }
$Executable = (Resolve-Path $Executable).Path
$output = Join-Path $root 'x64/f355c-results'
New-Item -ItemType Directory -Force $output | Out-Null
$results = [Collections.Generic.List[object]]::new()

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
    return $Log.Contains('[depth-frame] hash=9dd37d87b8eb202d')
}

function PacketCounts([string]$Log, [int]$Visible, [int]$Draws) {
    return $Log.Contains("[packet-draw] visible=$Visible draws=$Draws")
}

function Plan([string]$Log, [int]$MaxLists, [int]$Lists, [int]$Batches, [int]$Draws) {
    return $Log.Contains("[packet-plan] maxLists=$MaxLists lists=$Lists batches=$Batches draws=$Draws")
}

function Recording([string]$Log, [int]$Lists, [int]$Total) {
    return $Log.Contains("[recording-draws] mode=packets lists=$Lists total=$Total expected=$Total")
}

function JobSummary([string]$Log) {
    return [regex]::Match($Log, '\[jobs\] stopped: starts=(\d+) submitted=(\d+) completed=(\d+) failures=(\d+) unobserved=(\d+) participants=(\d+)')
}

function JobsClean([string]$Log, [int]$Failures = 0) {
    $j = JobSummary $Log
    return $j.Success -and $j.Groups[1].Value -eq '1' -and
        $j.Groups[2].Value -eq $j.Groups[3].Value -and
        $j.Groups[4].Value -eq "$Failures" -and
        $j.Groups[5].Value -eq '0' -and $j.Groups[6].Value -eq '0'
}

function Record([object]$Run) {
    $results.Add([pscustomobject]@{ name=$Run.Name; pass=$true; exit=$Run.Exit; arguments=$Run.Arguments })
    $results | ConvertTo-Json | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    Write-Output "$($Run.Name) pass=True"
}

# Packet L=1 owner path, L=2 sequential and L=2 parallel must be byte-identical.
$r = Run 'l1-parallel-owner' '-drawsource packets -commandlists parallel -packetlists 1 -jobworkers 2 -depthtest'
Assert ($r.Exit -eq 0) 'l1: exit'
Assert (HasHash $r.Log) 'l1: hash'
Assert (PacketCounts $r.Log 2 2) 'l1: packet counts'
Assert (Plan $r.Log 1 1 1 2) 'l1: plan'
Assert (Recording $r.Log 1 2) 'l1: draw total'
Assert $r.Log.Contains('[packet-recording] parallel=0 tasks=0') 'l1: owner-only tasks'
Assert $r.Log.Contains('[recording] PASS mode=packets lists=1 frames=1') 'l1: recording'
Assert (NoValidation $r.Log) 'l1: validation'
Record $r

$r = Run 'l2-multi' '-drawsource packets -commandlists multi -packetlists 2 -depthtest'
Assert ($r.Exit -eq 0) 'l2-multi: exit'
Assert (HasHash $r.Log) 'l2-multi: hash'
Assert (PacketCounts $r.Log 2 2) 'l2-multi: packet counts'
Assert (Plan $r.Log 2 2 2 2) 'l2-multi: plan'
Assert (Recording $r.Log 2 2) 'l2-multi: draw total'
Assert $r.Log.Contains('[packet-recording] parallel=0 tasks=0') 'l2-multi: sequential tasks'
Assert $r.Log.Contains('[recording] PASS mode=packets lists=4 frames=1') 'l2-multi: recording'
Assert (NoValidation $r.Log) 'l2-multi: validation'
Record $r

foreach ($workers in 1,2,4) {
    $r = Run "l2-parallel-w$workers" "-drawsource packets -commandlists parallel -packetlists 2 -jobworkers $workers -depthtest"
    Assert ($r.Exit -eq 0) "l2-w${workers}: exit"
    Assert (HasHash $r.Log) "l2-w${workers}: hash"
    Assert (PacketCounts $r.Log 2 2) "l2-w${workers}: packet counts"
    Assert (Plan $r.Log 2 2 2 2) "l2-w${workers}: plan"
    Assert (Recording $r.Log 2 2) "l2-w${workers}: draw total"
    Assert $r.Log.Contains('[packet-recording] parallel=1 tasks=2') "l2-w${workers}: tasks"
    Assert $r.Log.Contains("[packet-recording] parallel=1 tasks=2 backgroundWorkers=$workers") "l2-w${workers}: workers"
    Assert (JobsClean $r.Log) "l2-w${workers}: jobs"
    Assert $r.Log.Contains('GPU drained before shutdown') "l2-w${workers}: GPU drain"
    Assert $r.Log.Contains('[recording] PASS mode=packets lists=4 frames=1') "l2-w${workers}: recording"
    Assert (NoValidation $r.Log) "l2-w${workers}: validation"
    Record $r
}

# L=4/8 request with the two-object scene: only two non-empty lists exist.
foreach ($lists in 4,8) {
    $r = Run "l$lists-two-objects" "-drawsource packets -commandlists parallel -packetlists $lists -jobworkers 2 -depthtest"
    Assert ($r.Exit -eq 0) "l${lists}: exit"
    Assert (HasHash $r.Log) "l${lists}: hash"
    Assert (PacketCounts $r.Log 2 2) "l${lists}: packet counts"
    Assert (Plan $r.Log $lists 2 2 2) "l${lists}: plan has only two lists"
    Assert (Recording $r.Log 2 2) "l${lists}: no fake lists"
    Assert $r.Log.Contains('[packet-recording] parallel=1 tasks=2') "l${lists}: tasks"
    Assert (NoValidation $r.Log) "l${lists}: validation"
    Record $r
}

# One visible packet: one list, no worker tasks even when parallel was requested.
$r = Run 'one-visible-l4' '-drawsource packets -commandlists parallel -packetlists 4 -jobworkers 2 -packetfault hidesecond -prepframes 2'
Assert ($r.Exit -eq 0) 'one: exit'
Assert (PacketCounts $r.Log 1 1) 'one: packet counts'
Assert (Plan $r.Log 4 1 1 1) 'one: plan'
Assert (Recording $r.Log 1 1) 'one: draw total'
Assert $r.Log.Contains('[packet-recording] parallel=0 tasks=0') 'one: no tasks'
Assert (NoValidation $r.Log) 'one: validation'
Record $r

# Empty frame: no lists, no tasks, background/depth oracle.
$r = Run 'empty-l4' '-drawsource packets -commandlists parallel -packetlists 4 -jobworkers 2 -packetfault hideall -backgroundtest'
Assert ($r.Exit -eq 0) 'empty: exit'
Assert $r.Log.Contains('[background-frame] PASS') 'empty: background/depth oracle'
Assert (PacketCounts $r.Log 0 0) 'empty: packet counts'
Assert (Plan $r.Log 4 0 0 0) 'empty: plan'
Assert (Recording $r.Log 1 0) 'empty: draw total'
Assert $r.Log.Contains('[packet-recording] parallel=0 tasks=0') 'empty: no tasks'
Assert (NoValidation $r.Log) 'empty: validation'
Record $r

# Multiple batches in one list, including deterministic order and DrawTotal.
$r = Run 'batch-limit-1' '-drawsource packets -packetbatchlimit 1 -depthtest'
Assert ($r.Exit -eq 0) 'batch: exit'
Assert (HasHash $r.Log) 'batch: hash'
Assert (PacketCounts $r.Log 2 2) 'batch: packet counts'
Assert (Plan $r.Log 1 1 2 2) 'batch: two batches in one list'
Assert (Recording $r.Log 1 2) 'batch: draw total'
Assert (NoValidation $r.Log) 'batch: validation'
Record $r

# CPU preparation modes feed the same parallel packet path.
foreach ($prep in 'serial','parallel','verify') {
    $r = Run "prep-$prep-parallel" "-drawsource packets -commandlists parallel -packetlists 2 -jobworkers 2 -renderprep $prep -depthtest"
    Assert ($r.Exit -eq 0) "prep-${prep}: exit"
    Assert (HasHash $r.Log) "prep-${prep}: hash"
    Assert (PacketCounts $r.Log 2 2) "prep-${prep}: packet counts"
    Assert (Recording $r.Log 2 2) "prep-${prep}: draw total"
    Assert (NoValidation $r.Log) "prep-${prep}: validation"
    Record $r
}

# Fault matrix: allocator, Reset, Draw, Close, interrupted Submit, worker throw.
$faults = @(
    @{ name='allocator-1'; stage='allocator'; code=0; list=1; marker='entered=2 exited=2 overlap=1'; failures=0 },
    @{ name='allocator-2'; stage='allocator'; code=0; list=2; marker='entered=2 exited=2 overlap=1'; failures=0 },
    @{ name='reset-1'; stage='reset'; code=1; list=1; marker='entered=2 exited=2 overlap=1'; failures=0 },
    @{ name='reset-2'; stage='reset'; code=1; list=2; marker='entered=2 exited=2 overlap=1'; failures=0 },
    @{ name='draw-1'; stage='record'; code=2; list=1; marker='entered=2 exited=2 overlap=1'; failures=0 },
    @{ name='draw-2'; stage='record'; code=2; list=2; marker='entered=2 exited=2 overlap=1'; failures=0 },
    @{ name='close-1'; stage='close'; code=3; list=1; marker='entered=2 exited=2 overlap=1'; failures=0 },
    @{ name='close-2'; stage='close'; code=3; list=2; marker='entered=2 exited=2 overlap=1'; failures=0 },
    @{ name='submit-1'; stage='submit'; code=4; list=1; marker='entered=0 exited=0 overlap=0'; failures=0 },
    @{ name='submit-2'; stage='submit'; code=4; list=2; marker='entered=1 exited=1 overlap=0'; failures=0 },
    @{ name='throw-1'; stage='throw'; code=5; list=1; marker='entered=2 exited=2 overlap=1'; failures=1 },
    @{ name='throw-2'; stage='throw'; code=5; list=2; marker='entered=2 exited=2 overlap=1'; failures=1 }
)
foreach ($fault in $faults) {
    $r = Run "fault-$($fault.name)" "-drawsource packets -commandlists parallel -packetlists 2 -jobworkers 2 -depthtest -recordfault $($fault.stage) $($fault.list)"
    Assert ($r.Exit -eq 0) "fault-$($fault.name): recovery exit"
    Assert $r.Log.Contains("[recording-recovery] PASS stage=$($fault.code) list=$($fault.list) submittedUnchanged=1") "fault-$($fault.name): recovery marker"
    Assert $r.Log.Contains('[recording] ABORTED mode=packets lists=4 frames=1') "fault-$($fault.name): aborted"
    Assert $r.Log.Contains("[parallel-fault] stage=") "fault-$($fault.name): fault marker"
    Assert $r.Log.Contains($fault.marker) "fault-$($fault.name): task completion"
    $accepted = if ($fault.code -eq 4) { $fault.list - 1 } else { 2 }
    Assert $r.Log.Contains("accepted=$accepted completed=$accepted groupComplete=1") "fault-$($fault.name): accepted/drained"
    Assert $r.Log.Contains('ABORTED drained=1') "fault-$($fault.name): no pending callbacks"
    Assert (JobsClean $r.Log $fault.failures) "fault-$($fault.name): jobs"
    Assert (([regex]::Matches($r.Log,'GPU drained before shutdown')).Count -eq 2) "fault-$($fault.name): GPU drain"
    Assert (NoValidation $r.Log) "fault-$($fault.name): validation"
    Record $r
}

# Binding/category faults are rejected before any GPU reset or submission.
$rejects = @('stalevisual','unknownobject','invalidrange','overflowrange','transparent','hud','unsupportedpso','worldtransform')
foreach ($fault in $rejects) {
    $r = Run "reject-$fault-parallel" "-drawsource packets -commandlists parallel -packetlists 4 -jobworkers 2 -packetfault $fault -prepframes 2"
    Assert ($r.Exit -ne 0) "reject-${fault}: expected failure"
    Assert ($r.Log -match '\[packet-draw\] FAIL' -or $r.Log -match '\[renderprep\] FAIL') "reject-${fault}: explicit failure"
    Assert ($r.Log -match '\[recording\] (PASS|ABORTED) mode=packets lists=1 frames=0') "reject-${fault}: frames=0"
    Assert (NoValidation $r.Log) "reject-${fault}: validation"
    Record $r
}

# Resize and re-initialize keep packet config and list ownership.
$r = Run 'resize-parallel' '-drawsource packets -commandlists parallel -packetlists 2 -jobworkers 2 -resizetest -cputest 60'
Assert ($r.Exit -eq 0) 'resize: exit'
Assert $r.Log.Contains('resizeSuccess=3') 'resize: resizeSuccess'
Assert $r.Log.Contains('Resized to 1024x768') 'resize: final resize'
Assert (PacketCounts $r.Log 2 2) 'resize: packet counts'
Assert $r.Log.Contains('GPU drained before shutdown') 'resize: GPU drain'
Assert (NoValidation $r.Log) 'resize: validation'
Record $r

$r = Run 'prepfault-parallel' '-drawsource packets -commandlists parallel -packetlists 2 -jobworkers 2 -prepfault'
Assert ($r.Exit -eq 0) 'reinit: exit'
Assert $r.Log.Contains('[prep-recovery] PASS') 'reinit: marker'
Assert (PacketCounts $r.Log 2 2) 'reinit: packet counts'
Assert $r.Log.Contains('GPU drained before shutdown') 'reinit: GPU drain'
Assert (NoValidation $r.Log) 'reinit: validation'
Record $r

# WM_TIMER path with packet recording: normal and one-shot timer fault.
$timer = Join-Path $PSScriptRoot 'run-wmtimer.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $timer -Executable $Executable -Mode parallel -CommandLists parallel -DrawSource packets -PacketLists 2 -Burst 20
if ($LASTEXITCODE -ne 0) { throw 'WM_TIMER packet success path failed' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $timer -Executable $Executable -Mode parallel -CommandLists parallel -DrawSource packets -PacketLists 2 -Burst 20 -TimerFault
if ($LASTEXITCODE -ne 0) { throw 'WM_TIMER packet fault path failed' }

Write-Output ("PACKET MULTILIST PASS: " + $results.Count + " cases + WM_TIMER 2/2")
