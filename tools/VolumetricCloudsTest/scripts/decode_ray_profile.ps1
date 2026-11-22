param(
    [Parameter(Mandatory = $true)]
    [string]$Bmp,
    [Parameter(Mandatory = $true)]
    [string]$OutCsv,
    [double]$ProbeColumn = 0.35,
    [string]$Rows = "180,260,340,420,500,580,660"
)

$ErrorActionPreference = "Stop"

$cscCandidates = @(
    "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe",
    "C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe"
)
$csc = $cscCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $csc) {
    throw "csc.exe not found; install .NET Framework 4.x or compile DecodeRayProfile.cs manually"
}

$source = Join-Path $PSScriptRoot "DecodeRayProfile.cs"
$exe = Join-Path $env:TEMP ("DecodeRayProfile_" + [Guid]::NewGuid().ToString("N") + ".exe")
& $csc /nologo "/out:$exe" $source | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "failed to compile DecodeRayProfile.cs"
}

try {
    & $exe (Resolve-Path $Bmp) $OutCsv $ProbeColumn $Rows
    if ($LASTEXITCODE -ne 0) {
        throw "DecodeRayProfile failed with exit $LASTEXITCODE"
    }
}
finally {
    Remove-Item $exe -Force -ErrorAction SilentlyContinue
}
