param([string]$Executable = '')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
if (!$Executable) { $Executable = Join-Path $root 'x64/ReleaseDX12/xrDX12Smoke.exe' }
$Executable = (Resolve-Path $Executable).Path
$output = Join-Path $root 'x64/f355a-results'
New-Item -ItemType Directory -Force $output | Out-Null
$results = [Collections.Generic.List[object]]::new()

function Run([string]$Name, [string]$Arguments) {
    $p = Start-Process $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (!$p.WaitForExit(60000)) { Stop-Process -Id $p.Id; throw "Timeout: $Name" }
    $log = Get-Content (Join-Path (Split-Path $Executable) 'xrDX12Smoke.log') -Raw
    Set-Content (Join-Path $output "$Name.log") $log -Encoding utf8
    return [pscustomobject]@{ Name=$Name; Exit=$p.ExitCode; Log=$log; Arguments=$Arguments }
}

function Assert([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}

function NoValidation([string]$Log) {
    return $Log -notmatch '\[validation\]\[(WARNING|ERROR|CORRUPTION)\]'
}

function PacketDraw([string]$Log, [int]$Visible, [int]$Draws) {
    return $Log.Contains("[packet-draw] visible=$Visible draws=$Draws")
}

function RejectedBeforeSubmit([string]$Log) {
    return ($Log -match '\[packet-draw\] FAIL' -or $Log -match '\[renderprep\] FAIL') -and
        ($Log -match '\[recording\] (PASS|ABORTED) mode=packets lists=1 frames=0')
}

function Record([object]$Run) {
    $results.Add([pscustomobject]@{ name=$Run.Name; pass=$true; exit=$Run.Exit; arguments=$Run.Arguments })
    $results | ConvertTo-Json | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    Write-Output "$($Run.Name) pass=True"
}

# Baseline and deterministic ordering.
$r = Run 'baseline-two' '-drawsource packets -depthtest'
Assert ($r.Exit -eq 0) 'baseline: exit'
Assert $r.Log.Contains('[depth-frame] hash=9dd37d87b8eb202d') 'baseline: hash'
Assert (PacketDraw $r.Log 2 2) 'baseline: packet counts'
Assert $r.Log.Contains('[recording-draws] mode=packets lists=1 total=2 expected=2') 'baseline: draw total'
Assert $r.Log.Contains('[recording] PASS mode=packets lists=1 frames=1') 'baseline: recording'
Assert $r.Log.Contains('GPU-based validation=1') 'baseline: GBV'
Assert (NoValidation $r.Log) 'baseline: validation'
Record $r

$r = Run 'ordering-reverse' '-drawsource packets -packetfault reverse -depthtest'
Assert ($r.Exit -eq 0) 'reverse: exit'
Assert $r.Log.Contains('[depth-frame] hash=9dd37d87b8eb202d') 'reverse: hash'
Assert (PacketDraw $r.Log 2 2) 'reverse: packet counts'
Assert (NoValidation $r.Log) 'reverse: validation'
Record $r

# Empty / one visible object and culling.
$r = Run 'empty-visible' '-drawsource packets -packetfault hideall -prepframes 5'
Assert ($r.Exit -eq 0) 'empty: exit'
Assert (PacketDraw $r.Log 0 0) 'empty: packet counts'
Assert $r.Log.Contains('[recording-draws] mode=packets lists=1 total=0 expected=0') 'empty: draw total'
Assert $r.Log.Contains('[recording] PASS mode=packets lists=1 frames=5') 'empty: recording'
Assert (NoValidation $r.Log) 'empty: validation'
Record $r

$r = Run 'empty-background-oracle' '-drawsource packets -packetfault hideall -backgroundtest'
Assert ($r.Exit -eq 0) 'background: exit'
Assert $r.Log.Contains('[background-frame] PASS') 'background: pixel/depth oracle'
Assert (PacketDraw $r.Log 0 0) 'background: packet counts'
Assert (NoValidation $r.Log) 'background: validation'
Record $r

$r = Run 'one-visible-flag' '-drawsource packets -packetfault hidesecond -prepframes 5'
Assert ($r.Exit -eq 0) 'one-flag: exit'
Assert (PacketDraw $r.Log 1 1) 'one-flag: packet counts'
Assert $r.Log.Contains('[recording-draws] mode=packets lists=1 total=1 expected=1') 'one-flag: draw total'
Assert (NoValidation $r.Log) 'one-flag: validation'
Record $r

$r = Run 'one-visible-frustum' '-drawsource packets -packetfault cullsecond -prepframes 5'
Assert ($r.Exit -eq 0) 'one-cull: exit'
Assert (PacketDraw $r.Log 1 1) 'one-cull: packet counts'
Assert (NoValidation $r.Log) 'one-cull: validation'
Record $r

# Unsupported categories and resource bindings: reject before submission.
foreach ($fault in @('transparent','hud','unsupportedpso','worldtransform','unknownobject','stalevisual','invalidrange','overflowrange')) {
    $r = Run "reject-$fault" "-drawsource packets -packetfault $fault -prepframes 5"
    Assert ($r.Exit -ne 0) "reject-${fault}: expected non-zero exit"
    Assert (RejectedBeforeSubmit $r.Log) "reject-${fault}: expected explicit failure with frames=0"
    Assert (NoValidation $r.Log) "reject-${fault}: validation"
    Record $r
}

# CPU preparation modes feed the same packet path.
foreach ($prep in @('parallel','verify')) {
    $r = Run "prep-$prep" "-drawsource packets -renderprep $prep -jobworkers 2 -depthtest"
    Assert ($r.Exit -eq 0) "prep-${prep}: exit"
    Assert $r.Log.Contains('[depth-frame] hash=9dd37d87b8eb202d') "prep-${prep}: hash"
    Assert (PacketDraw $r.Log 2 2) "prep-${prep}: packet counts"
    Assert (NoValidation $r.Log) "prep-${prep}: validation"
    Record $r
}

# Resize and shutdown/reinitialize recovery.
$r = Run 'resize' '-drawsource packets -resizetest -cputest 60'
Assert ($r.Exit -eq 0) 'resize: exit'
Assert $r.Log.Contains('resizeSuccess=3') 'resize: resizeSuccess'
Assert $r.Log.Contains('Resized to 1024x768') 'resize: final resize'
Assert (PacketDraw $r.Log 2 2) 'resize: packet counts'
Assert (NoValidation $r.Log) 'resize: validation'
Record $r

$r = Run 'prep-recovery' '-drawsource packets -prepfault'
Assert ($r.Exit -eq 0) 'recovery: exit'
Assert $r.Log.Contains('[prep-recovery] PASS') 'recovery: marker'
Assert (PacketDraw $r.Log 2 2) 'recovery: packet counts'
Assert (NoValidation $r.Log) 'recovery: validation'
Record $r

# Invalid CLI combinations must be rejected explicitly.
$invalid = @(
    @{ name='reject-parallel'; args='-drawsource packets -commandlists parallel -prepframes 5'; text='single owner command list only' },
    @{ name='reject-draws'; args='-drawsource packets -draws 128 -prepframes 5'; text='cannot be mixed with -draws/-lists' },
    @{ name='reject-lists'; args='-drawsource packets -lists 2 -prepframes 5'; text='cannot be mixed with -draws/-lists' },
    @{ name='reject-fault-without-source'; args='-packetfault hideall -prepframes 5'; text='requires -drawsource packets' }
)
foreach ($case in $invalid) {
    $r = Run $case.name $case.args
    Assert ($r.Exit -ne 0) "$($case.name): expected non-zero exit"
    Assert $r.Log.Contains($case.text) "$($case.name): expected rejection text"
    Record $r
}

Write-Output ("PACKET DRAW PASS: " + $results.Count + " cases")
