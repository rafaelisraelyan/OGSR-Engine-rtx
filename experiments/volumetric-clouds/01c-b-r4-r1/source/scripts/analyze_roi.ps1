param(
    [Parameter(Mandatory = $true)]
    [string]$Image,
    [int]$X0,
    [int]$Y0,
    [int]$X1,
    [int]$Y1,
    [int]$Threshold = 114,
    [ValidateSet("dark", "bright")]
    [string]$Mode = "dark",
    [string]$Label = ""
)

$ErrorActionPreference = "Stop"

$cscCandidates = @(
    "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe",
    "C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe"
)
$csc = $cscCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $csc) {
    throw "csc.exe not found; install .NET Framework 4.x"
}

if ([string]::IsNullOrEmpty($Label)) {
    $Label = [System.IO.Path]::GetFileName($Image)
}

$source = Join-Path $PSScriptRoot "AnalyzeRoi.cs"
$exe = Join-Path $env:TEMP ("AnalyzeRoi_" + [Guid]::NewGuid().ToString("N") + ".exe")
& $csc /nologo /r:System.Drawing.dll "/out:$exe" $source | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "failed to compile AnalyzeRoi.cs"
}

try {
    & $exe (Resolve-Path $Image) $X0 $Y0 $X1 $Y1 $Threshold $Mode $Label
    if ($LASTEXITCODE -ne 0) {
        throw "AnalyzeRoi failed with exit $LASTEXITCODE"
    }
}
finally {
    Remove-Item $exe -Force -ErrorAction SilentlyContinue
}
