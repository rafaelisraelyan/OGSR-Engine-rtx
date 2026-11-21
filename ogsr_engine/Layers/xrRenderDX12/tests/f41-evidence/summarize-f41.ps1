# F4.1 run summary: static scene, frames, diagnostics.
#
# Reads appdata\logs\xray_gr_*.log of one run directory and writes
# summary.json + summary.md next to it. Read-only with respect to the log,
# the frames and the run manifest.
#
# ASCII-only on purpose: Windows PowerShell 5.1 misreads UTF-8 without BOM.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RunDirectory
)

$ErrorActionPreference = 'Stop'
$logs = Get-ChildItem (Join-Path $RunDirectory 'appdata\logs') -Filter 'xray_gr_*.log' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending
if (-not $logs) { throw "No game log under $RunDirectory" }
$log = $logs[0]
$lines = Get-Content $log.FullName

function First-Match([string]$pattern) {
    $m = $lines | Select-String -Pattern $pattern | Select-Object -First 1
    if ($m) { return $m.Line.Trim() } else { return '' }
}
function All-Matches([string]$pattern) {
    return @($lines | Select-String -Pattern $pattern | ForEach-Object { $_.Line.Trim() })
}

$snapshot  = First-Match 'ph_dx12_f41: snapshot'
$hierarchy = First-Match 'ph_dx12_f41: hierarchy'
$pools     = First-Match 'ph_dx12_f41: pools'
$reach     = First-Match 'ph_dx12_f41: reachability'
$rejected  = First-Match 'ph_dx12_f41: rejected_'
$textures  = First-Match 'ph_dx12_f41: textures unique'
$ready     = First-Match 'ph_dx12_f41: ready'
$loaded    = First-Match 'ph_dx12_f41: scene loaded'
$infoInit  = First-Match 'infoqueue phase=init'
$infoShut  = First-Match 'infoqueue phase=shutdown'
$failure   = All-Matches 'ph_dx12_f41: load failed'
$declPools = All-Matches 'ph_dx12_f41: vb_pool '
$texMiss   = All-Matches 'ph_dx12_f41: texture missing'
$texUnsup  = All-Matches 'ph_dx12_f41: texture unsupported'
$metrics   = All-Matches 'ph_dx12_metrics: frame='

$drawCounts = @()
foreach ($line in $metrics) {
    $m = [regex]::Match($line, 'draws=(\d+)')
    if ($m.Success) { $drawCounts += [int]$m.Groups[1].Value }
}

$drawsMin = $null
$drawsMax = $null
$drawsAvg = $null
if ($drawCounts.Count -gt 0) {
    $drawsMin = ($drawCounts | Measure-Object -Minimum).Minimum
    $drawsMax = ($drawCounts | Measure-Object -Maximum).Maximum
    $drawsAvg = [math]::Round(($drawCounts | Measure-Object -Average).Average, 1)
}

$summary = [ordered]@{
    run                  = Split-Path $RunDirectory -Leaf
    log                  = $log.Name
    snapshot             = $snapshot
    hierarchy            = $hierarchy
    pools                = $pools
    reachability         = $reach
    rejected             = $rejected
    textures             = $textures
    scene_ready          = $ready
    scene_loaded         = $loaded
    load_failures        = $failure.Count
    vb_pools             = $declPools.Count
    textures_missing     = $texMiss.Count
    textures_unsupported = $texUnsup.Count
    metric_frames        = $metrics.Count
    draws_min            = $drawsMin
    draws_avg            = $drawsAvg
    draws_max            = $drawsMax
    infoqueue_init       = $infoInit
    infoqueue_shutdown   = $infoShut
}

$outJson = Join-Path $RunDirectory 'summary.json'
$summary | ConvertTo-Json -Depth 4 | Set-Content -Path $outJson -Encoding ASCII

$md = @()
$md += '# F4.1 run summary'
$md += ''
$md += 'run: ' + $summary.run
$md += 'log: ' + $summary.log
$md += ''
$md += '| item | value |'
$md += '|---|---|'
$md += '| snapshot | ' + $snapshot + ' |'
$md += '| hierarchy | ' + $hierarchy + ' |'
$md += '| pools | ' + $pools + ' |'
$md += '| reachability | ' + $reach + ' |'
$md += '| rejected | ' + $rejected + ' |'
$md += '| textures | ' + $textures + ' |'
$md += '| scene loaded | ' + $loaded + ' |'
$md += '| metric frames | ' + $metrics.Count + ' |'
$md += '| draws min/avg/max | ' + $drawsMin + ' / ' + $drawsAvg + ' / ' + $drawsMax + ' |'
$md += '| VB pools | ' + $declPools.Count + ' |'
$md += '| texture missing | ' + $texMiss.Count + ' |'
$md += '| texture unsupported | ' + $texUnsup.Count + ' |'
$md += '| load failures | ' + $failure.Count + ' |'
$md += ''
$md += '## InfoQueue'
$md += ''
$md += '```'
$md += $infoInit
$md += $infoShut
$md += '```'
Set-Content -Path (Join-Path $RunDirectory 'summary.md') -Value $md -Encoding ASCII

Write-Output ('summary written for ' + $summary.run)
