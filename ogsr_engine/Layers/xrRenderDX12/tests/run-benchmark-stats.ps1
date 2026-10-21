$ErrorActionPreference = 'Stop'
[Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::InvariantCulture
$runner = Join-Path $PSScriptRoot 'run-scaling-benchmark.ps1'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($runner, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Benchmark runner parse failed' }
# Загружаем только реальные функции статистики, без запуска GPU benchmark.
foreach ($name in 'Get-Percentile', 'Get-Stats') {
    $function = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true)
    if (!$function) { throw "Missing function $name" }
    Invoke-Expression $function.Extent.Text
}
function Check([bool]$Condition, [string]$Name) {
    if (!$Condition) { throw "FAIL: $Name" }
}
$empty = Get-Stats -Values @()
Check ($empty.Count -eq 0 -and $null -eq $empty.P99) 'empty p99'
$one = Get-Stats -Values @(3.5)
Check ($one.P99 -eq 3.5) 'singleton p99'
$ordered = Get-Stats -Values @(1..100)
Check ([math]::Abs($ordered.P99 - 99.01) -lt 1e-10) 'interpolated p99'
Check ([math]::Abs($ordered.P95 - 95.05) -lt 1e-10) 'p95 unchanged'
$mixed = Get-Stats -Values @(5.0, [double]::NaN, 1.0, 3.0)
Check ($mixed.Count -eq 3 -and [math]::Abs($mixed.P99 - 4.96) -lt 1e-10) 'sorting and invalid samples'
$csv = $ordered | ConvertTo-Csv -NoTypeInformation | ConvertFrom-Csv
Check ([math]::Abs([double]$csv.P99 - 99.01) -lt 1e-10) 'p99 serialization'
Write-Output 'BENCHMARK STATS PASS: 6 checks'
