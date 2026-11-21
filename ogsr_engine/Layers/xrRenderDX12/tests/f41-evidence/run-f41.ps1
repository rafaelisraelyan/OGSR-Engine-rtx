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
    [string]$RunsRoot = '',
    [string]$Exe = '',
    [string]$GameData = 'C:\OGSR_J1_Runtime_R2\gamedata',
    [string]$ArchRoot = 'C:\OGSR_J1_Runtime_R2',
    [string]$Save = 'b23_jupiter',
    [string]$Demo = '',
    [switch]$Capture,
    [switch]$NoClose,
    [int]$TimeoutSeconds = 120,
    [ValidateRange(1, 600)][int]$ShutdownTimeoutSeconds = 20,
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
    [string]$WindowShotReadyPattern = '',
    [int]$ViewMode = 0,
    [hashtable]$Env = @{}
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..'))
if ($Run -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_.-]*$' -or $Run.EndsWith('.')) {
    throw "Invalid run identifier: $Run"
}
if ($RunsRoot -eq '') { $RunsRoot = Join-Path $PSScriptRoot 'runs' }
$RunsRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($RunsRoot)
$dir = Join-Path $RunsRoot $Run

if (Test-Path -LiteralPath $dir) {
    throw "Run directory already exists: $dir"
}
if ($Exe -eq '') {
    $Exe = Join-Path $repo 'bin_x64\xrEngine.exe'
}
$Exe = (Get-Item -LiteralPath $Exe).FullName
$binaryRoot = Split-Path -Parent $Exe
$GameData = (Get-Item -LiteralPath $GameData).FullName
$ArchRoot = (Get-Item -LiteralPath $ArchRoot).FullName
if (!(Test-Path -LiteralPath (Join-Path $ArchRoot 'levels') -PathType Container)) {
    throw "Level archives are missing under: $ArchRoot"
}
$sourceSave = Join-Path $ArchRoot "appdata\savedgames\$Save.scop"
if (!(Test-Path -LiteralPath $sourceSave -PathType Leaf)) {
    throw "Requested save is missing: $sourceSave"
}
if ($Demo -ne '') {
    $Demo = (Get-Item -LiteralPath $Demo).FullName
    if ([IO.Path]::GetExtension($Demo) -ine '.xrdemo') { throw "Expected an .xrdemo file: $Demo" }
}
if ($WindowShot -ne '') {
    $WindowShot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($WindowShot)
    if (Test-Path -LiteralPath $WindowShot) { throw "Window screenshot already exists: $WindowShot" }
}
if ($WindowShotReadyPattern -ne '') { $null = [regex]::new($WindowShotReadyPattern) }

New-Item -ItemType Directory -Force -Path $RunsRoot | Out-Null
New-Item -ItemType Directory -Path $dir | Out-Null
$exeName = "xrEngine_$Run.exe"
Copy-Item -LiteralPath $Exe -Destination (Join-Path $dir $exeName)

foreach ($dll in @('nvngx_dlss.dll', 'OpenAL32.dll', 'dbghelp.dll')) {
    $source = Join-Path $binaryRoot $dll
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        Copy-Item -LiteralPath $source -Destination $dir
    }
}

# gamedata РїСЂРѕРіРѕРЅР° = РїРѕР»РЅР°СЏ РєРѕРїРёСЏ РёРіСЂРѕРІРѕРіРѕ gamedata СЂСЏРґРѕРј СЃ EXE ($fs_root$).
$overlayLog = Join-Path $dir 'overlay-copy.log'
$PSNativeCommandUseErrorActionPreference = $false
robocopy $GameData (Join-Path $dir 'gamedata') /E /COPY:DAT /XJ /R:1 /W:1 /NP /NFL /NDL |
    Out-File -FilePath $overlayLog -Encoding utf8
if ($LASTEXITCODE -ge 8) {
    throw "Overlay copy failed (robocopy exit $LASTEXITCODE). See $overlayLog"
}

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
if ($Demo -ne '') { Copy-Item -LiteralPath $Demo -Destination (Join-Path $appdata 'savedgames') }

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

function Get-GameLog {
    Get-ChildItem -LiteralPath (Join-Path $dir 'appdata\logs') -Filter 'xray_gr_*.log' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
}

$started = Get-Date
$savedEnvironment = @{}
try {
    foreach ($key in $environment.Keys) {
        $savedEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, [string]$environment[$key], 'Process')
    }
    $process = Start-Process -FilePath (Join-Path $dir $exeName) -WorkingDirectory $dir `
        -ArgumentList ($arguments -join ' ') -PassThru
} finally {
    foreach ($key in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($key, $savedEnvironment[$key], 'Process')
    }
}
Write-Output "launched pid=$($process.Id) run directory: $dir"
$peakWorkingSet = 0
$peakPrivate = 0
$deadline = $started.AddSeconds($TimeoutSeconds)
$windowShotDone = $false
$windowShotError = ''
$closeForced = $false
while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    try {
        $process.Refresh()
        if ($process.WorkingSet64 -gt $peakWorkingSet) { $peakWorkingSet = $process.WorkingSet64 }
        if ($process.PrivateMemorySize64 -gt $peakPrivate) { $peakPrivate = $process.PrivateMemorySize64 }
    } catch { }
    if ($WindowShot -ne '' -and -not $windowShotDone -and
        ((Get-Date) - $started).TotalSeconds -ge $WindowShotDelay) {
        if ($WindowShotReadyPattern -ne '') {
            $currentLog = Get-GameLog
            if (!$currentLog -or !(Select-String -LiteralPath $currentLog.FullName -Pattern $WindowShotReadyPattern -Quiet -ErrorAction SilentlyContinue)) {
                continue
            }
        }
        try {
            & (Join-Path $PSScriptRoot 'capture-window.ps1') -ProcessId $process.Id -OutputPath $WindowShot
            $windowShotDone = $true
            $windowShotError = ''
        } catch { $windowShotError = $_.Exception.Message }
    }
}
$closeRequested = $null
$closeAccepted = $null
if (-not $process.HasExited -and -not $NoClose) {
    $closeRequested = Get-Date
    $closeAccepted = $process.CloseMainWindow()
    if (-not $process.WaitForExit($ShutdownTimeoutSeconds * 1000)) {
        try {
            $process.Kill()
            $closeForced = $true
        } catch {
            if (!$process.HasExited) { throw }
        }
        $null = $process.WaitForExit(5000)
    }
}
$exitCode = if ($process.HasExited) { $process.ExitCode } else { -1 }
$completed = Get-Date

$logFile = Get-GameLog
$windowShotHash = if ($windowShotDone) { (Get-FileHash -LiteralPath $WindowShot -Algorithm SHA256).Hash } else { '' }

$manifest = @(
    "run=$Run"
    "processId=$($process.Id)"
    "renderer=$(if ($R4) { 'R4' } else { 'native DX12 requested' })"
    "exe=$exeName"
    "exe_sha256=$((Get-FileHash (Join-Path $dir $exeName) -Algorithm SHA256).Hash)"
    "args=$($arguments -join ' ')"
    "env=$((($environment.GetEnumerator() | Sort-Object Key | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ';'))"
    "gamedata=$GameData"
    "archives=$ArchRoot"
    "save=$Save"
    "demo=$Demo"
    "started=$($started.ToString('o'))"
    "closeRequested=$($closeRequested)"
    "closeAccepted=$closeAccepted"
    "closeForced=$closeForced"
    "completed=$($completed.ToString('o'))"
    "exit=$exitCode"
    "cleanExit=$($process.HasExited -and $exitCode -eq 0 -and !$closeForced)"
    "windowShot=$WindowShot"
    "windowShotDone=$windowShotDone"
    "windowShotSha256=$windowShotHash"
    "windowShotError=$windowShotError"
    "peakWorkingSet=$peakWorkingSet"
    "sampledPeakPrivate=$peakPrivate"
    "log=$($logFile.Name)"
)
Set-Content -Path (Join-Path $dir 'manifest.md') -Value $manifest -Encoding utf8

Write-Output "run directory: $dir"
Write-Output "exit=$exitCode log=$($logFile.FullName)"
