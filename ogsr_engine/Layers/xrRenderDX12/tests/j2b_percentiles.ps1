param(
    [Parameter(Mandatory = $true)][string]$LogPath
)

$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Get-NearestRankValue {
    param([double[]]$Sorted, [double]$Percentile)
    if ($Sorted.Count -eq 0) { throw 'empty sample set' }
    if ($Percentile -lt 0.0 -or $Percentile -gt 1.0) {
        throw "percentile out of range: $Percentile"
    }
    # Nearest-rank: index = ceil(p * N), 1-based, clamped to [1, N].
    $rank = [int][Math]::Ceiling($Percentile * $Sorted.Count)
    if ($rank -lt 1) { $rank = 1 }
    if ($rank -gt $Sorted.Count) { $rank = $Sorted.Count }
    return $Sorted[$rank - 1]
}

function Get-MetricValues {
    param([string]$Name)
    $pattern = "$Name=([0-9]+(?:\.[0-9]+)?)"
    $values = @()
    foreach ($line in Get-Content $LogPath) {
        if ($line -match 'M1_VFS_BATCH') {
            $m = [regex]::Match($line, $pattern)
            if ($m.Success) {
                $values += [double]::Parse($m.Groups[1].Value, $invariant)
            }
        }
    }
    return $values
}

function Write-Metric {
    param([string]$Name, [double[]]$Values)
    if ($Values.Count -eq 0) {
        Write-Output "$Name samples=0"
        return
    }
    $sorted = @($Values | Sort-Object)
    $min = Get-NearestRankValue -Sorted $sorted -Percentile 0.0
    $p50 = Get-NearestRankValue -Sorted $sorted -Percentile 0.50
    $p95 = Get-NearestRankValue -Sorted $sorted -Percentile 0.95
    $p99 = Get-NearestRankValue -Sorted $sorted -Percentile 0.99
    $max = Get-NearestRankValue -Sorted $sorted -Percentile 1.0
    Write-Output ([string]::Format(
        $invariant,
        '{0} samples={1} min={2} p50={3} p95={4} p99={5} max={6}',
        $Name, $sorted.Count, $min, $p50, $p95, $p99, $max))
}

Write-Output 'J2B nearest-rank percentiles (index = ceil(p*N), 1-based)'
Write-Output "log=$LogPath"
Write-Metric -Name 'total_ms' -Values (Get-MetricValues -Name 'total_ms')
Write-Metric -Name 'vfs_copy_ms' -Values (Get-MetricValues -Name 'vfs_copy_ms')
Write-Metric -Name 'snapshot_hash_ms' -Values (Get-MetricValues -Name 'snapshot_hash_ms')
Write-Metric -Name 'parse_ms' -Values (Get-MetricValues -Name 'parse_ms')
Write-Metric -Name 'gr_wait_ms' -Values (Get-MetricValues -Name 'gr_wait_ms')

# Boundary self-check: rank(0)=min, rank(1)=max.
$sample = @(1.0, 2.0, 3.0, 4.0)
$boundaryOk =
    ((Get-NearestRankValue -Sorted $sample -Percentile 0.0) -eq 1.0) -and
    ((Get-NearestRankValue -Sorted $sample -Percentile 0.5) -eq 2.0) -and
    ((Get-NearestRankValue -Sorted $sample -Percentile 1.0) -eq 4.0)
Write-Output "boundary_check=$(if ($boundaryOk) { 'PASS' } else { 'FAIL' })"
if (-not $boundaryOk) { exit 1 }
