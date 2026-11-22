$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$repo = "D:\Coding Project\OGSR ENGINE fork from rtx"
$final = Join-Path $repo "tools\VolumetricCloudsTest\artifacts\v23h_a03b_r02\final"
$outDir = Join-Path $final "sheets"
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force $outDir | Out-Null

function New-Sheet {
    param(
        [array]$Items,
        [int]$Columns,
        [int]$CellWidth,
        [int]$CellHeight,
        [string]$OutPath
    )
    $rows = [int][math]::Ceiling($Items.Count / $Columns)
    $labelH = 24
    $pad = 8
    $w = $Columns * $CellWidth
    $h = $rows * $CellHeight
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $font = New-Object System.Drawing.Font("Segoe UI", 10)
    $brush = [System.Drawing.Brushes]::White
    for ($i = 0; $i -lt $Items.Count; $i++) {
        $col = $i % $Columns
        $row = [int][math]::Floor($i / $Columns)
        $x = $col * $CellWidth
        $y = $row * $CellHeight
        $img = [System.Drawing.Image]::FromFile($Items[$i].Path)
        $availW = $CellWidth - 2 * $pad
        $availH = $CellHeight - $labelH - 2 * $pad
        $scale = [math]::Min($availW / $img.Width, $availH / $img.Height)
        $dw = [int]($img.Width * $scale)
        $dh = [int]($img.Height * $scale)
        $dx = $x + [int](($CellWidth - $dw) / 2)
        $dy = $y + $pad
        $g.DrawImage($img, $dx, $dy, $dw, $dh)
        $g.DrawString($Items[$i].Label, $font, $brush, $x + $pad, $y + $CellHeight - $labelH + 3)
        $img.Dispose()
    }
    $bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bmp.Dispose()
}

New-Sheet -Items @(
    @{Path = (Join-Path $final "hcs1\local_s1337\xz_y2600_system_s1337.png"); Label = "Level 1: system influence (XZ, y=2600 m)"},
    @{Path = (Join-Path $final "hcs1\local_s1337\xz_y2600_mass_s1337.png"); Label = "Level 2: mass organization (XZ, y=2600 m)"},
    @{Path = (Join-Path $final "hcs1\local_s1337\xz_y2600_vertical_s1337.png"); Label = "Level 3: vertical structure (XZ, y=2600 m)"},
    @{Path = (Join-Path $final "hcs1\local_s1337\xz_y2600_occ_s1337.png"); Label = "3D occupancy slice (XZ, y=2600 m)"},
    @{Path = (Join-Path $final "hcs1\local_s1337\xzmax_s1337.png"); Label = "Column-max occupancy, 16 km"},
    @{Path = (Join-Path $final "hcs1\local_s1337\xy_s1337.png"); Label = "XY slice, world aspect (16 x 5.4 km)"},
    @{Path = (Join-Path $final "hcs1\local_s1337\yz_s1337.png"); Label = "YZ slice, world aspect (16 x 5.4 km)"},
    @{Path = (Join-Path $final "hcs1\local_s1337\profiles_s1337.png"); Label = "Vertical occupancy profiles (x=columns, y=height)"},
    @{Path = (Join-Path $final "hcs1\local_s1337\top_surface_s1337.png"); Label = "Cloud-top hillshade preview"}
) -Columns 3 -CellWidth 380 -CellHeight 330 -OutPath (Join-Path $outDir "SHEET_hcs1_local_s1337.png")

New-Sheet -Items @(
    @{Path = (Join-Path $final "hcs1\regional_s1337\xz_y2600_system_s1337.png"); Label = "HCS1 60 km system influence, y=2600 m"},
    @{Path = (Join-Path $final "hcs1\regional_s1337\xz_y2600_mass_s1337.png"); Label = "HCS1 60 km mass organization, y=2600 m"},
    @{Path = (Join-Path $final "hcs1\regional_s1337\xz_y2600_vertical_s1337.png"); Label = "HCS1 60 km vertical structure, y=2600 m"},
    @{Path = (Join-Path $final "hcs1\regional_s1337\xzmax_s1337.png"); Label = "HCS1 60 km column-max, seed 1337"},
    @{Path = (Join-Path $final "hcs1\regional_s1337\xy_s1337.png"); Label = "HCS1 60 km XY, world aspect"},
    @{Path = (Join-Path $final "hcs1\regional_s1337\yz_s1337.png"); Label = "HCS1 60 km YZ, world aspect"}
) -Columns 3 -CellWidth 380 -CellHeight 330 -OutPath (Join-Path $outDir "SHEET_hcs1_60km_s1337.png")

$seedItems = @()
foreach ($seed in @(1337, 4242, 99, 2027, 9001, 9529)) {
    $seedItems += @{Path = (Join-Path $final ("hcs1\local_s{0}\xzmax_s{0}.png" -f $seed)); Label = ("HCS1 local 16 km, seed {0}" -f $seed)}
}
New-Sheet -Items $seedItems -Columns 3 -CellWidth 380 -CellHeight 330 -OutPath (Join-Path $outDir "SHEET_hcs1_seeds_local.png")

$seedItems = @()
foreach ($seed in @(1337, 4242, 99, 2027, 9001, 9529)) {
    $seedItems += @{Path = (Join-Path $final ("hcs1\regional_s{0}\xzmax_s{0}.png" -f $seed)); Label = ("HCS1 regional 60 km, seed {0}" -f $seed)}
}
New-Sheet -Items $seedItems -Columns 3 -CellWidth 380 -CellHeight 330 -OutPath (Join-Path $outDir "SHEET_hcs1_seeds_regional.png")

New-Sheet -Items @(
    @{Path = (Join-Path $final "hcs2\regional_s1337\xzmax_s1337.png"); Label = "HCS2 negative control, 60 km, seed 1337"},
    @{Path = (Join-Path $final "hcs2\regional_s1337\xy_s1337.png"); Label = "HCS2 60 km XY, world aspect"},
    @{Path = (Join-Path $final "hcs2\regional_s1337\yz_s1337.png"); Label = "HCS2 60 km YZ, world aspect"},
    @{Path = (Join-Path $final "hcs2\local_s1337\xzmax_s1337.png"); Label = "HCS2 local 16 km, seed 1337"}
) -Columns 2 -CellWidth 560 -CellHeight 360 -OutPath (Join-Path $outDir "SHEET_hcs2_60km_s1337.png")

New-Sheet -Items @(
    @{Path = (Join-Path $final "r01_corrected\r01_xy_corrected_s1337.png"); Label = "R01 XY corrected to 16:2.8 world aspect"},
    @{Path = (Join-Path $final "r01_corrected\r01_yz_corrected_s1337.png"); Label = "R01 YZ corrected to 16:2.8 world aspect"}
) -Columns 1 -CellWidth 900 -CellHeight 240 -OutPath (Join-Path $outDir "SHEET_r01_aspect_correction.png")

Write-Output "SHEETS_DONE"
Get-ChildItem $outDir -File | Select-Object Name, Length
