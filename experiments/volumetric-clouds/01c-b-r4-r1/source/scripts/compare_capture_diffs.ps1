param(
    [string]$CaptureRoot = "D:\Coding Project\OGSR ENGINE fork from rtx\tools\VolumetricCloudsTest\artifacts\v23h_capture",
    [string]$OutFile = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

if (-not $OutFile) {
    $OutFile = Join-Path $CaptureRoot "legacy_vs_reference_diffs.csv"
}

# Returns "mean,max" as invariant text. Never returns a PSCustomObject: the
# previous PowerShell analysis exported System.Object[] because the caller
# captured an unrolled object array instead of two numeric fields.
function Compare-Bitmaps([string]$aPath, [string]$bPath) {
    $a = [System.Drawing.Bitmap]::FromFile($aPath)
    $b = [System.Drawing.Bitmap]::FromFile($bPath)
    try {
        if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) {
            throw "size mismatch: $aPath vs $bPath"
        }
        $sum = 0.0
        $count = 0
        $maximum = 0
        for ($y = 0; $y -lt $a.Height; $y += 4) {
            for ($x = 0; $x -lt $a.Width; $x += 4) {
                $ca = $a.GetPixel($x, $y)
                $cb = $b.GetPixel($x, $y)
                foreach ($delta in @(
                    [math]::Abs($ca.R - $cb.R),
                    [math]::Abs($ca.G - $cb.G),
                    [math]::Abs($ca.B - $cb.B))) {
                    $sum += $delta
                    $count++
                    if ($delta -gt $maximum) { $maximum = $delta }
                }
            }
        }
        $mean = $sum / [double]$count
        return [string]::Format(
            $invariant, "{0:R},{1}", [double]$mean, [int]$maximum)
    }
    finally {
        $a.Dispose()
        $b.Dispose()
    }
}

$scenes = @("sunset", "storm", "overcast")
$references = @(
    @{ Name = "h12_base"; Root = "h12\{0}\base\c0.45" },
    @{ Name = "h12_height"; Root = "h12\{0}\height\c0.45" },
    @{ Name = "h3_scenes_coverage"; Root = "h3_scenes\{0}\coverage\c0.45" },
    @{ Name = "h4_erosion"; Root = "h4\{0}\erosion\c0.45" }
)
$modes = @("final", "macro_shape")

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("Scene,Mode,Reference,MeanAbs,Max")
foreach ($scene in $scenes) {
    foreach ($mode in $modes) {
        $legacy = Join-Path $CaptureRoot "legacy\$scene\none\c0.45\$mode\$mode.png"
        if (-not (Test-Path $legacy)) { throw "missing legacy image: $legacy" }
        foreach ($reference in $references) {
            $relative = [string]::Format($reference.Root, $scene)
            $path = Join-Path $CaptureRoot "$relative\$mode\$mode.png"
            if (-not (Test-Path $path)) { continue }
            $pair = Compare-Bitmaps $legacy $path
            $parts = $pair -split ","
            $mean = [double]::Parse($parts[0], $invariant)
            $maximum = [int]::Parse($parts[1], $invariant)
            $lines.Add([string]::Format(
                $invariant, "{0},{1},{2},{3:R},{4}",
                $scene, $mode, $reference.Name, $mean, $maximum))
        }
    }
}

[System.IO.File]::WriteAllLines(
    $OutFile, $lines, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "wrote $OutFile rows=$($lines.Count - 1)"
Get-Content $OutFile -Encoding UTF8 | Select-Object -First 8
