param(
    [Parameter(Mandatory=$true)][string]$Ogf,
    [Parameter(Mandatory=$true)][string]$Dds,
    [string]$Executable = ''
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$Ogf = (Resolve-Path $Ogf).Path
$Dds = (Resolve-Path $Dds).Path
if (!$Executable) { $Executable = Join-Path $root 'x64/ReleaseDX12/xrDX12Smoke.exe' }
$Executable = (Resolve-Path $Executable).Path
$output = Join-Path $root 'x64/f36a-results'
New-Item -ItemType Directory -Force $output | Out-Null
$results = [Collections.Generic.List[object]]::new()
$texturedHash = '[xray-frame] hash=829633b7dc067e7a'
$fallbackHash = '[xray-frame] hash=79bff891dbd7af25'

function Run([string]$Name, [string]$Arguments) {
    $logPath = Join-Path (Split-Path $Executable) 'xrDX12Smoke.log'
    Remove-Item $logPath -ErrorAction SilentlyContinue
    $p = Start-Process $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (!$p.WaitForExit(180000)) { Stop-Process -Id $p.Id -Force; throw "Timeout: $Name" }
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

function XrayPass([string]$Log) {
    return $Log.Contains('[smoke] -xraytest: PASS') -and
        $Log.Contains('[xray-frame] PASS')
}

function PacketCounts([string]$Log, [int]$Visible, [int]$Draws) {
    return $Log.Contains("[packet-draw] visible=$Visible draws=$Draws")
}

function JobsClean([string]$Log) {
    $m = [regex]::Match($Log, '\[jobs\] stopped: starts=(\d+) submitted=(\d+) completed=(\d+) failures=(\d+) unobserved=(\d+) participants=(\d+)')
    return $m.Success -and $m.Groups[1].Value -eq '1' -and
        $m.Groups[2].Value -eq $m.Groups[3].Value -and
        $m.Groups[4].Value -eq '0' -and $m.Groups[5].Value -eq '0'
}

function Record([object]$Run) {
    $results.Add([pscustomobject]@{ name=$Run.Name; pass=$true; exit=$Run.Exit; arguments=$Run.Arguments })
    $results | ConvertTo-Json | Set-Content (Join-Path $output 'results.json') -Encoding utf8
    Write-Output "$($Run.Name) pass=True"
}

$common = "-scene xrayasset -drawsource packets -xrayogf `"$Ogf`" -xraydds `"$Dds`" -xraytest"

$r = Run 'textured-l1' $common
Assert ($r.Exit -eq 0) 'textured-l1: exit'
Assert (XrayPass $r.Log) 'textured-l1: oracle'
Assert (PacketCounts $r.Log 4 4) 'textured-l1: packet counts'
Assert $r.Log.Contains($texturedHash) 'textured-l1: hash'
Assert $r.Log.Contains('actual(140,116,107,255)') 'textured-l1: center pixel'
Assert $r.Log.Contains('center depth min=0.984311') 'textured-l1: depth'
Assert (NoValidation $r.Log) 'textured-l1: validation'
Record $r

$r = Run 'textured-l1-eight-frames' ($common + ' -xrayframes 8')
Assert ($r.Exit -eq 0) 'textured-l1-8: exit'
Assert (XrayPass $r.Log) 'textured-l1-8: oracle'
Assert $r.Log.Contains($texturedHash) 'textured-l1-8: hash'
Assert $r.Log.Contains('[recording] PASS mode=packets lists=1 frames=8') 'textured-l1-8: frames'
Assert (NoValidation $r.Log) 'textured-l1-8: validation'
Record $r

$r = Run 'textured-l2-parallel-w2' ($common + ' -commandlists parallel -packetlists 2 -jobworkers 2 -xrayframes 8')
Assert ($r.Exit -eq 0) 'textured-l2: exit'
Assert (XrayPass $r.Log) 'textured-l2: oracle'
Assert (PacketCounts $r.Log 4 4) 'textured-l2: packet counts'
Assert $r.Log.Contains('[packet-plan] maxLists=2 lists=2') 'textured-l2: plan'
Assert $r.Log.Contains('[packet-recording] parallel=1 tasks=2') 'textured-l2: tasks'
Assert $r.Log.Contains($texturedHash) 'textured-l2: hash'
Assert (JobsClean $r.Log) 'textured-l2: jobs'
Assert (NoValidation $r.Log) 'textured-l2: validation'
Record $r

$r = Run 'textured-l4-parallel' ($common + ' -commandlists parallel -packetlists 4 -jobworkers 4 -xrayframes 4')
Assert ($r.Exit -eq 0) 'textured-l4: exit'
Assert (XrayPass $r.Log) 'textured-l4: oracle'
Assert $r.Log.Contains('[packet-plan] maxLists=4 lists=4') 'textured-l4: plan'
Assert $r.Log.Contains($texturedHash) 'textured-l4: hash'
Assert (JobsClean $r.Log) 'textured-l4: jobs'
Assert (NoValidation $r.Log) 'textured-l4: validation'
Record $r

$r = Run 'textured-batch-limit-1' ($common + ' -packetbatchlimit 1 -xrayframes 4')
Assert ($r.Exit -eq 0) 'batch: exit'
Assert (XrayPass $r.Log) 'batch: oracle'
Assert $r.Log.Contains('[packet-plan] maxLists=1 lists=1 batches=4') 'batch: four batches'
Assert $r.Log.Contains($texturedHash) 'batch: hash'
Assert (NoValidation $r.Log) 'batch: validation'
Record $r

$fallbackArgs = "-scene xrayasset -drawsource packets -xrayogf `"$Ogf`" -xraytest"
$r = Run 'fallback-texture' $fallbackArgs
Assert ($r.Exit -eq 0) 'fallback: exit'
Assert (XrayPass $r.Log) 'fallback: oracle'
Assert $r.Log.Contains($fallbackHash) 'fallback: hash'
Assert $r.Log.Contains('actual(255,128,32,255)') 'fallback: orange pixel'
Assert ($r.Log -notmatch 'hash=829633b7dc067e7a') 'fallback: distinct from textured hash'
Assert (NoValidation $r.Log) 'fallback: validation'
Record $r

$unicodeStem = 'f36a_' + [char]0x0442 + [char]0x0435 + [char]0x0441 + [char]0x0442
$unicodeOgf = Join-Path $env:TEMP ($unicodeStem + '.ogf')
$unicodeDds = Join-Path $env:TEMP ($unicodeStem + '.dds')
Copy-Item $Ogf $unicodeOgf -Force
Copy-Item $Dds $unicodeDds -Force
$r = Run 'unicode-path' "-scene xrayasset -drawsource packets -xrayogf `"$unicodeOgf`" -xraydds `"$unicodeDds`" -xraytest"
Assert ($r.Exit -eq 0) 'unicode-path: exit'
Assert (XrayPass $r.Log) 'unicode-path: oracle'
Assert $r.Log.Contains($texturedHash) 'unicode-path: hash'
Assert (NoValidation $r.Log) 'unicode-path: validation'
Record $r
Remove-Item $unicodeOgf,$unicodeDds -Force

$spaceOgf = Join-Path $env:TEMP 'f36a path with spaces.ogf'
$spaceDds = Join-Path $env:TEMP 'f36a path with spaces.dds'
Copy-Item $Ogf $spaceOgf -Force
Copy-Item $Dds $spaceDds -Force
$r = Run 'space-path' "-scene xrayasset -drawsource packets -xrayogf `"$spaceOgf`" -xraydds `"$spaceDds`" -xraytest"
Assert ($r.Exit -eq 0) 'space-path: exit'
Assert (XrayPass $r.Log) 'space-path: oracle'
Assert $r.Log.Contains($texturedHash) 'space-path: hash'
Assert (NoValidation $r.Log) 'space-path: validation'
Record $r
Remove-Item $spaceOgf,$spaceDds -Force

$bad = Join-Path $env:TEMP 'f36a_missing_asset.ogf'
Remove-Item $bad -ErrorAction SilentlyContinue
$r = Run 'reject-invalid-ogf' "-scene xrayasset -drawsource packets -xrayogf `"$bad`" -xraytest"
Assert ($r.Exit -ne 0) 'invalid-ogf: expected failure'
Assert ($r.Log -match 'Initialize failed at X-Ray OGF') 'invalid-ogf: explicit failure'
Assert ($r.Log -notmatch '\[DX12\]\[xray\] asset uploaded') 'invalid-ogf: no GPU upload'
Assert (NoValidation $r.Log) 'invalid-ogf: validation'
Record $r

$badDds = Join-Path $env:TEMP 'f36a_invalid_explicit.dds'
Set-Content $badDds 'not a dds' -Encoding ascii
$r = Run 'explicit-invalid-dds' "-scene xrayasset -drawsource packets -xrayogf `"$Ogf`" -xraydds `"$badDds`" -xraytest"
Assert ($r.Exit -ne 0) 'explicit-invalid-dds: expected failure'
Assert ($r.Log -match 'explicit X-Ray DDS failed to load') 'explicit-invalid-dds: diagnostic'
Assert ($r.Log -notmatch '\[smoke\] -xraytest: PASS') 'explicit-invalid-dds: no false PASS'
Assert ($r.Log -notmatch '\[xray-frame\] PASS') 'explicit-invalid-dds: no fallback oracle PASS'
Assert (NoValidation $r.Log) 'explicit-invalid-dds: validation'
Record $r

$emptyDdsArgs = '-scene xrayasset -drawsource packets -xrayogf "' + $Ogf + '" -xraydds "" -xraytest'
$r = Run 'empty-explicit-dds' $emptyDdsArgs
Assert ($r.Exit -ne 0) 'empty-explicit-dds: expected failure'
Assert ($r.Log -match '\-xraydds requires a non-empty path') 'empty-explicit-dds: diagnostic'
Assert ($r.Log -notmatch '\[DX12\]\[xray\] asset uploaded') 'empty-explicit-dds: no GPU upload'
Assert ($r.Log -notmatch '\[xray-frame\] PASS') 'empty-explicit-dds: no oracle PASS'
Assert ($r.Log -notmatch '\[smoke\] -xraytest: PASS') 'empty-explicit-dds: no false PASS'
Assert (NoValidation $r.Log) 'empty-explicit-dds: validation'
Record $r

$missingDdsArgs = '-scene xrayasset -drawsource packets -xrayogf "' + $Ogf + '" -xraydds -xraytest'
$r = Run 'missing-dds-path-before-option' $missingDdsArgs
Assert ($r.Exit -ne 0) 'missing-dds-path-before-option: expected failure'
Assert ($r.Log -match '\-xraydds requires a non-empty path') 'missing-dds-path-before-option: diagnostic'
Assert ($r.Log -notmatch '\[DX12\]\[xray\] asset uploaded') 'missing-dds-path-before-option: no GPU upload'
Assert ($r.Log -notmatch '\[xray-frame\] PASS') 'missing-dds-path-before-option: no frame oracle PASS'
Assert ($r.Log -notmatch '\[smoke\] -xraytest: PASS') 'missing-dds-path-before-option: no false PASS'
Assert (NoValidation $r.Log) 'missing-dds-path-before-option: validation'
Record $r

$missingDdsCpuBenchArgs = '-scene xrayasset -drawsource packets -xrayogf "' + $Ogf + '" -xraydds -cpubench 60 -xraytest'
$r = Run 'missing-dds-path-before-cpubench' $missingDdsCpuBenchArgs
Assert ($r.Exit -eq 1) 'missing-dds-path-before-cpubench: exit 1'
Assert ($r.Log -match '\-xraydds requires a non-empty path') 'missing-dds-path-before-cpubench: diagnostic'
Assert ($r.Log -notmatch '\[DX12\]\[xray\] asset uploaded') 'missing-dds-path-before-cpubench: no GPU upload'
Assert ($r.Log -notmatch '\[xray-frame\] PASS') 'missing-dds-path-before-cpubench: no frame oracle PASS'
Assert ($r.Log -notmatch '\[smoke\] -xraytest: PASS') 'missing-dds-path-before-cpubench: no false PASS'
Assert (NoValidation $r.Log) 'missing-dds-path-before-cpubench: validation'
Record $r

$r = Run 'post-submit-failure-drain' "-scene xrayasset -drawsource packets -xrayogf `"$Ogf`" -xraydds `"$Dds`" -xraypostsubmitfault"
Assert ($r.Exit -ne 0) 'post-submit: expected Initialize failure'
Assert ($r.Log -match '\[xray-init-fault\] post-submit failure') 'post-submit: fault marker'
Assert ($r.Log -match '\[DX12\] GPU drained before shutdown') 'post-submit: safe drain'
Assert ($r.Log -notmatch 'LIMITATION: GPU drain failed') 'post-submit: no unknown drain'
Assert ($r.Log -match '\[DX12\] Shutdown complete') 'post-submit: shutdown completed'
Assert (NoValidation $r.Log) 'post-submit: validation'
Record $r

$r = Run 'resize-xray' "-scene xrayasset -drawsource packets -xrayogf `"$Ogf`" -xraydds `"$Dds`" -resizetest -cputest 60"
Assert ($r.Exit -eq 0) 'resize: exit'
Assert ($r.Log -match 'resizeSuccess=([3-9]|\d{2,})') 'resize: at least three resizes'
Assert (NoValidation $r.Log) 'resize: validation'
Record $r

$r = Run 'reinit-xray' "-scene xrayasset -drawsource packets -xrayogf `"$Ogf`" -xraydds `"$Dds`" -prepfault"
Assert ($r.Exit -eq 0) 'reinit: exit'
Assert $r.Log.Contains('[prep-recovery] PASS') 'reinit: recovery'
Assert (NoValidation $r.Log) 'reinit: validation'
Record $r

Write-Output ("XRAY SCENE PASS: " + $results.Count + " cases")
