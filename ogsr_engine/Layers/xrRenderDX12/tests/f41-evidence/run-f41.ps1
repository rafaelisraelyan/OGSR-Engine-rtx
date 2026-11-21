# F4.1 вЂ” РїРѕРґРіРѕС‚РѕРІРєР° Рё Р·Р°РїСѓСЃРє native DX12 РїСЂРѕРіРѕРЅР° РЅР° Jupiter.
#
# Р’РѕСЃРїСЂРѕРёР·РІРѕРґРёС‚ РїСЂРѕС†РµРґСѓСЂСѓ F4.0-B (СЃРј. artifacts/F4.0-B-VSLICE/runs) Р±РµР·
# СЂСѓС‡РЅС‹С… С€Р°РіРѕРІ: run dir, gamedata-РѕРІРµСЂР»РµР№, fsgame.ltx, appdata, Р·Р°РїСѓСЃРє,
# С€С‚Р°С‚РЅРѕРµ Р·Р°РєСЂС‹С‚РёРµ Рё РјР°РЅРёС„РµСЃС‚ РїСЂРѕРіРѕРЅР°.
#
# Usage:
#   .\run-f41.ps1 -Run f41-jupiter-01
#   .\run-f41.ps1 -Run f41-jupiter-02 -Capture -TimeoutSeconds 180

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Run,
    [string]$Exe = '',
    [string]$GameData = 'C:\OGSR_J1_Runtime_R2\gamedata',
    [string]$ArchRoot = 'C:\OGSR_J1_Runtime_R2',
    [string]$Save = 'b23_jupiter',
    [switch]$Capture,
    [switch]$NoClose,
    [int]$TimeoutSeconds = 120,
    [int]$Width = 1280,
    [int]$Height = 720,
    [string]$Route = '',
    [string]$Fly = '',
    [switch]$NoCull,
    [switch]$NoRasterCull,
    [switch]$NoAlphaTest,
    [switch]$R4,
    [switch]$Cmp,
    [string]$WindowShot = '',
    [int]$WindowShotDelay = 18,
    [int]$ViewMode = 0,
    [hashtable]$Env = @{}
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$runsRoot = Join-Path $PSScriptRoot 'runs'
$dir = Join-Path $runsRoot $Run

if (Test-Path $dir) {
    throw "Run directory already exists: $dir"
}
if ($Exe -eq '') {
    $Exe = Join-Path $repo 'bin_x64\xrEngine.exe'
}
if (!(Test-Path $Exe)) {
    throw "Engine executable is missing: $Exe"
}
if (!(Test-Path $GameData)) {
    throw "Game data is missing: $GameData"
}
if (!(Test-Path (Join-Path $ArchRoot 'levels'))) {
    throw "Level archives are missing under: $ArchRoot"
}

New-Item -ItemType Directory -Force -Path $dir | Out-Null
$exeName = "xrEngine_$Run.exe"
Copy-Item $Exe (Join-Path $dir $exeName)

foreach ($dll in @('nvngx_dlss.dll', 'OpenAL32.dll')) {
    $source = Join-Path $repo "bin_x64\$dll"
    if (Test-Path $source) { Copy-Item $source $dir }
}

# gamedata РїСЂРѕРіРѕРЅР° = РїРѕР»РЅР°СЏ РєРѕРїРёСЏ РёРіСЂРѕРІРѕРіРѕ gamedata СЂСЏРґРѕРј СЃ EXE ($fs_root$).
$overlayLog = Join-Path $dir 'overlay-copy.log'
robocopy $GameData (Join-Path $dir 'gamedata') /E /COPY:DAT /R:1 /W:1 /NP /NFL /NDL |
    Out-File -FilePath $overlayLog -Encoding utf8

$appdata = Join-Path $dir 'appdata'
New-Item -ItemType Directory -Force -Path (Join-Path $appdata 'savedgames') | Out-Null
$userLtx = Join-Path $ArchRoot 'appdata\user_ogsr.ltx'
if (Test-Path $userLtx) {
    $text = Get-Content $userLtx -Raw
    $text = [regex]::Replace($text, '(?im)^(\s*vid_mode\s+).*$', "`${1}${Width}x${Height}")
    $text = [regex]::Replace($text, '(?im)^(\s*rs_fullscreen\s+).*$', '${1}off')
    Set-Content -Path (Join-Path $appdata 'user_ogsr.ltx') -Value $text -Encoding default
}
foreach ($known in @("$Save.scop", 'b23_zaton.scop')) {
    $source = Join-Path $ArchRoot "appdata\savedgames\$known"
    if (Test-Path $source) {
        Copy-Item $source (Join-Path $appdata 'savedgames')
    }
}

$fsgame = @"
`$app_data_root$		= true|		false|		$dir\appdata\
`$arch_dir$              = false| false| $ArchRoot\
`$game_arch_mp$          = false| false| $ArchRoot\|            mp\
`$arch_dir_levels$       = false| false| $ArchRoot\|            levels\
`$arch_dir_resources$    = false| false| $ArchRoot\|            resources\
`$arch_dir_localization$ = false| false| $ArchRoot\|            localization\
`$arch_dir_patches$      = false| true|  $ArchRoot\|            patches\
`$game_data$   		= false|	true|	`$fs_root$|		gamedata\
`$game_ai$    		= true|		false|		`$game_data$|		ai\
`$game_spawn$    	= true|		false|		`$game_data$|		spawns\
`$game_levels$   	= true|		false|		`$game_data$|		levels\
`$game_meshes$    	= true|		true|		`$game_data$|		meshes\|       *.ogf;*.omf|
`$game_anims$   		= true|		true|		`$game_data$|		anims\|        *.anm;*.anms|
`$game_dm$    		= true|		true|		`$game_data$|		meshes\|       *.dm|
`$game_shaders$    	= true|		true|		`$game_data$|		shaders\
`$game_sounds$    	= true|		true|		`$game_data$|		sounds\
`$game_textures$    	= true|		true|		`$game_data$|		textures\
`$game_config$    	= true|		false|		`$game_data$|		configs\
`$game_weathers$         = true|  false| `$game_config$|        environment\weathers
`$game_weather_effects$  = true|  false| `$game_config$|        environment\weather_effects
`$textures$              = true|  true|  `$game_data$|          textures\
`$level$      		= false|	false|	`$game_levels$
`$game_scripts$          = true|  false| `$game_data$|          scripts\|      *.script|
`$logs$      		= true|		false|	`$app_data_root$|	logs\
`$screenshots$    	= true|		false|	`$app_data_root$|	screenshots\
`$game_saves$    	= true|		false|	`$app_data_root$|	savedgames\
`$downloads$             = false| false| `$app_data_root$|
"@
Set-Content -Path (Join-Path $dir 'fsgame.ltx') -Value $fsgame -Encoding default

$arguments = @(
    '-fsltx fsgame.ltx', '-nogameintro', '-center_screen', '-log'
)
if (-not $R4) { $arguments += '-dx12_native_game' }
if ($Capture) { $arguments += '-b2_camera_capture' }
$arguments += "-start server($Save/single/alife/load)"
$environment = @{
    OGSR_DX12_GAME_GR      = 'verify'
    OGSR_DX12_GAME_METRICS = '1'
    OGSR_DX12_GAME_SCENE_LOG = '1'
}
if ($Capture) { $environment['OGSR_DX12_NATIVE_CAPTURE_DIR'] = $dir }
if ($Fly -ne '') { $environment['OGSR_DX12_GAME_FLY'] = $Fly }
if ($NoCull) { $environment['OGSR_DX12_GAME_SCENE_CULL'] = '0' }
if ($NoRasterCull) { $environment['OGSR_DX12_GAME_SCENE_CULL'] = 'none' }
if ($NoAlphaTest) { $environment['OGSR_DX12_GAME_SCENE_NOTEST'] = '1' }
if ($Cmp) { $environment['OGSR_DX12_CMP'] = '1' }
if ($ViewMode -ne 0) { $environment['OGSR_DX12_GAME_SCENE_VIEW'] = [string]$ViewMode }
foreach ($key in $Env.Keys) { $environment[$key] = $Env[$key] }
foreach ($key in $environment.Keys) {
    [Environment]::SetEnvironmentVariable($key, [string]$environment[$key], 'Process')
}

$started = Get-Date
$process = Start-Process -FilePath (Join-Path $dir $exeName) -WorkingDirectory $dir `
    -ArgumentList ($arguments -join ' ') -PassThru
$peakWorkingSet = 0
$peakPrivate = 0
$deadline = $started.AddSeconds($TimeoutSeconds)
$closed = $false
$windowShotDone = $false
while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    try {
        $process.Refresh()
        if ($process.WorkingSet64 -gt $peakWorkingSet) { $peakWorkingSet = $process.WorkingSet64 }
        if ($process.PrivateMemorySize64 -gt $peakPrivate) { $peakPrivate = $process.PrivateMemorySize64 }
    } catch { }
    if ($WindowShot -ne '' -and -not $windowShotDone -and
        ((Get-Date) - $started).TotalSeconds -ge $WindowShotDelay) {
        $windowShotDone = $true
        try {
            & powershell.exe -NoProfile -ExecutionPolicy Bypass `
                -File (Join-Path $PSScriptRoot 'capture-window.ps1') `
                -ProcessId $process.Id -OutputPath $WindowShot 2>&1 | Out-Null
        } catch { }
    }
}
$closeRequested = $null
$closeAccepted = $null
if (-not $process.HasExited -and -not $NoClose) {
    $closeRequested = Get-Date
    $closeAccepted = $process.CloseMainWindow()
    if (-not $process.WaitForExit(20000)) {
        $process.Kill()
        $process.WaitForExit(5000)
    }
}
$exitCode = if ($process.HasExited) { $process.ExitCode } else { -1 }
$completed = Get-Date

$logFile = Get-ChildItem (Join-Path $dir 'appdata\logs') -Filter 'xray_gr_*.log' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1

$manifest = @(
    "run=$Run"
    "exe=$exeName"
    "exe_sha256=$((Get-FileHash (Join-Path $dir $exeName) -Algorithm SHA256).Hash)"
    "args=$($arguments -join ' ')"
    "env=$((($environment.GetEnumerator() | Sort-Object Key | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ';'))"
    "started=$($started.ToString('o'))"
    "closeRequested=$($closeRequested)"
    "closeAccepted=$closeAccepted"
    "completed=$($completed.ToString('o'))"
    "exit=$exitCode"
    "peakWorkingSet=$peakWorkingSet"
    "sampledPeakPrivate=$peakPrivate"
    "log=$($logFile.Name)"
)
Set-Content -Path (Join-Path $dir 'manifest.md') -Value $manifest -Encoding utf8

Write-Output "run directory: $dir"
Write-Output "exit=$exitCode log=$($logFile.FullName)"
