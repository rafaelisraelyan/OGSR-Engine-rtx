# Build a compact archive with the F4.1 screenshots and reports.
# ASCII-only on purpose: Windows PowerShell 5.1 misreads UTF-8 without BOM.

[CmdletBinding()]
param(
    [string]$Root = '',
    [string]$OutputDirectory = '',
    # Optional R4 reference frame for the side-by-side comparison.
    [string]$R4Reference = ''
)

$ErrorActionPreference = 'Stop'
if ($Root -eq '') { $Root = $PSScriptRoot }
if ($OutputDirectory -eq '') { $OutputDirectory = Join-Path $Root 'dist' }
if (!(Test-Path $OutputDirectory)) { New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null }

# Runs whose full 30-frame capture sets are shipped. The f41-final* runs are the
# current full-coverage state; f41-cmp-08 is the run that first reached 13911.
$canonical = @(
    'f41-final-a-actorcam',
    'f41-final-b-street',
    'f41-final-e-air2',
    'f41-cmp-08'
)

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$name = 'OGSR-F4.1-screenshots-and-reports-' + $stamp
$stage = Join-Path $env:TEMP $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }

$shots = Join-Path $stage 'screenshots'
$reports = Join-Path $stage 'reports'
$summaries = Join-Path $stage 'run-summaries'
New-Item -ItemType Directory -Force -Path $shots | Out-Null
New-Item -ItemType Directory -Force -Path $reports | Out-Null
New-Item -ItemType Directory -Force -Path $summaries | Out-Null

$evidence = Join-Path $Root 'evidence'
if (Test-Path $evidence) {
    Copy-Item -Path (Join-Path $evidence '*.png') -Destination $shots -Force -ErrorAction SilentlyContinue
}

foreach ($run in $canonical) {
    $source = Join-Path $Root ('runs\' + $run + '\frames')
    if (!(Test-Path $source)) { continue }
    $destination = Join-Path $shots $run
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -Path (Join-Path $source '*.png') -Destination $destination -Force -ErrorAction SilentlyContinue
    $frameManifest = Join-Path $source 'manifest.json'
    if (Test-Path $frameManifest) {
        Copy-Item -LiteralPath $frameManifest -Destination $destination -Force
    }
}

# R4 reference frame used by the side-by-side comparison, when supplied.
if ($R4Reference -ne '' -and (Test-Path $R4Reference)) {
    $r4Directory = Join-Path $shots 'r4-reference'
    New-Item -ItemType Directory -Force -Path $r4Directory | Out-Null
    Copy-Item -LiteralPath $R4Reference -Destination (Join-Path $r4Directory 'r4-jupiter-interior.png') -Force
}

# Reports and docs.
$reportFiles = @('F4.1E-Comparison-Report.md', 'F4.1-Implementation-Report.md')
foreach ($file in $reportFiles) {
    $source = Join-Path $Root $file
    if (Test-Path $source) { Copy-Item -LiteralPath $source -Destination $reports -Force }
}
$docSource = Join-Path (Split-Path -Parent (Split-Path -Parent $Root)) 'docs\dx12-f41-full-static-scene.md'
if (Test-Path $docSource) {
    Copy-Item -LiteralPath $docSource -Destination (Join-Path $reports 'dx12-f41-full-static-scene.md') -Force
}

# Per-run summaries and the comparison log excerpt, so the numbers are auditable.
foreach ($run in @($canonical + @('f41-final-d-details'))) {
    $runDirectory = Join-Path $Root ('runs\' + $run)
    if (!(Test-Path $runDirectory)) { continue }
    $summaryScript = Join-Path $Root 'summarize-f41.ps1'
    if (Test-Path $summaryScript) {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $summaryScript -RunDirectory $runDirectory 2>&1 | Out-Null
    }
    foreach ($file in @('summary.json', 'summary.md', 'manifest.md')) {
        $source = Join-Path $runDirectory $file
        if (Test-Path $source) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $summaries ($run + '-' + $file)) -Force
        }
    }
    $logDirectory = Join-Path $runDirectory 'appdata\logs'
    if (Test-Path $logDirectory) {
        $log = Get-ChildItem $logDirectory -Filter 'xray_gr_*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($log) {
            $keep = Select-String -Path $log.FullName -Pattern 'ph_cmp:|ph_dx12_f41:' |
                ForEach-Object { $_.Line }
            Set-Content -Path (Join-Path $summaries ($run + '-evidence.log')) -Value $keep -Encoding ASCII
        }
    }
}

$pngCount = (Get-ChildItem $stage -Recurse -Filter '*.png' | Measure-Object).Count

$index = @()
$index += '# F4.1 screenshots and reports'
$index += ''
$index += ('built: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz'))
$index += ''
$index += '## Key frames'
$index += ''
$index += '- screenshots/05-compare-r4-vs-dx12-interior.png - same camera, R4 left, DX12 right'
$index += '- screenshots/06-aerial-full-coverage.png - whole map after the LOD and TREE_PM fixes'
$index += '- screenshots/07-street-full-coverage.png - ground level, full coverage'
$index += '- screenshots/08-interior-full-coverage.png - interior used for the R4 comparison'
$index += '- screenshots/01..04 - earlier key frames'
$index += '- screenshots/r4-reference/r4-jupiter-interior.png - the R4 half of the comparison'
$index += ''
$index += '## Full capture sets (30 frames each, Present 120..149)'
$index += ''
foreach ($run in $canonical) { $index += ('- screenshots/' + $run + '/') }
$index += ''
$index += 'Each folder carries manifest.json with per-frame SHA-256, Present, fence, slot.'
$index += ''
$index += '## Reports'
$index += ''
$index += '- reports/F4.1E-Comparison-Report.md - this stage: R4 vs DX12 inventory, gaps, fixes'
$index += '- reports/F4.1-Implementation-Report.md - previous stage'
$index += '- reports/dx12-f41-full-static-scene.md - architecture and format notes'
$index += ''
$index += '## Audit trail'
$index += ''
$index += 'run-summaries/ holds per-run summary.json, summary.md, manifest.md and an'
$index += 'evidence.log with every ph_cmp/ph_dx12_f41 line of that run, so each number'
$index += 'in the report can be traced back to a log line.'
$index += ''
$index += ('total PNG: ' + $pngCount)
Set-Content -Path (Join-Path $stage 'INDEX.md') -Value $index -Encoding ASCII

$sums = @()
foreach ($file in (Get-ChildItem $stage -Recurse -File | Sort-Object FullName)) {
    $relative = ($file.FullName.Substring($stage.Length + 1)) -replace '\\', '/'
    if ($relative -eq 'SHA256SUMS.txt') { continue }
    $sums += ((Get-FileHash $file.FullName -Algorithm SHA256).Hash + '  ' + $relative)
}
Set-Content -Path (Join-Path $stage 'SHA256SUMS.txt') -Value $sums -Encoding ASCII

$zip = Join-Path $OutputDirectory ($name + '.zip')
if (Test-Path $zip) { Remove-Item -Force $zip }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stream = [System.IO.File]::Open($zip, [System.IO.FileMode]::CreateNew)
$archive = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
foreach ($file in (Get-ChildItem $stage -Recurse -File | Sort-Object FullName)) {
    $relative = ($file.FullName.Substring($stage.Length + 1)) -replace '\\', '/'
    $entry = $archive.CreateEntry(($name + '/' + $relative), [System.IO.Compression.CompressionLevel]::Optimal)
    $entryStream = $entry.Open()
    $input = [System.IO.File]::OpenRead($file.FullName)
    $input.CopyTo($entryStream)
    $input.Dispose()
    $entryStream.Dispose()
}
$archive.Dispose()
$stream.Dispose()

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
Set-Content -Path ($zip + '.sha256') -Value ($hash + '  ' + (Split-Path $zip -Leaf)) -Encoding ASCII

Write-Output ('zip:    ' + $zip)
Write-Output ('png:    ' + $pngCount)
Write-Output ('files:  ' + (Get-ChildItem $stage -Recurse -File | Measure-Object).Count)
Write-Output ('size:   ' + [math]::Round((Get-Item $zip).Length / 1MB, 1) + ' MB')
Write-Output ('sha256: ' + $hash)
