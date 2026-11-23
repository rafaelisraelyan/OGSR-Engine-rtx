[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GameRoot,
    [string]$Exe,
    [switch]$R4,
    [switch]$NoLitDecals,
    [switch]$LegacyAlpha,
    [switch]$NoWater,
    [switch]$NoPuddles,
    [switch]$NoDynamicSunShadows,
    [switch]$NoSunShadows,
    [switch]$NoLocalShadows,
    [switch]$NoHemi,
    [switch]$NoTerrainDetail,
    [switch]$NoShadows,
    [switch]$NoLocalLights,
    [switch]$NoTransparency,
    [uint32[]]$GeometryVisualId,
    [Nullable[uint32]]$GeometryObjectId,
    [switch]$SceneLog,
    [switch]$Validation,
    [uint64]$LightId,
    [string]$FixedLightIds,
    [switch]$FreezeLightRecord,
    [ValidateRange(0,600)][uint32]$ProbeFrames,
    [ValidateRange(0,3600)][uint32]$ProbeDelay,
    [string]$ProbeOrigin,
    [ValidateSet('Default','ShadowDepth','ShadowVisibility','ShadowSlot','Albedo','Normals',
        'Lighting','Hemi','WaterNormals','WaterReflection','Material','LightTerms',
        'LocalRaw','LocalProbe','LightIndex','WorldPosition')]
    [string]$View = 'Default'
)

$ErrorActionPreference = 'Stop'
if ($FixedLightIds -and $FreezeLightRecord) { throw 'Выберите один режим фиксации света.' }
if (($FixedLightIds -or $FreezeLightRecord) -and $NoLocalLights) { throw 'Для фиксации света требуется включённый local lighting.' }
if (($FreezeLightRecord -or $View -in @('LocalProbe','LightIndex')) -and $LightId -eq 0) {
    throw 'Для выбранного режима укажите -LightId.'
}
if ($FixedLightIds) {
    $ids = @($FixedLightIds.Split(',') | ForEach-Object { [uint64]::Parse($_, [Globalization.CultureInfo]::InvariantCulture) })
    if ($ids.Count -lt 2 -or $ids.Count -gt 3 -or $ids -contains 0 -or
        @($ids | Select-Object -Unique).Count -ne $ids.Count) {
        throw 'Укажите два или три разных ненулевых ID через запятую.'
    }
    $FixedLightIds = $ids -join ','
}
if ($View -eq 'WorldPosition' -and -not $ProbeOrigin) { throw 'Укажите -ProbeOrigin как X,Y,Z.' }
if ($ProbeOrigin) {
    $culture = [Globalization.CultureInfo]::InvariantCulture
    $components = @($ProbeOrigin.Split(',') | ForEach-Object {
        [single]::Parse($_, [Globalization.NumberStyles]::Float, $culture)
    })
    if ($components.Count -ne 3 -or @($components | Where-Object {
        [single]::IsNaN($_) -or [single]::IsInfinity($_)
    }).Count -ne 0) {
        throw 'Укажите три конечные координаты -ProbeOrigin как X,Y,Z.'
    }
    $ProbeOrigin = ($components | ForEach-Object { $_.ToString('R', $culture) }) -join ','
}
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $scriptDirectory = $PSScriptRoot
    if ([string]::IsNullOrWhiteSpace($scriptDirectory) -and $MyInvocation.MyCommand.Path) {
        $scriptDirectory = Split-Path -LiteralPath $MyInvocation.MyCommand.Path -Parent
    }
    if ([string]::IsNullOrWhiteSpace($scriptDirectory)) {
        throw 'Укажите путь к xrEngine.exe через -Exe.'
    }
    $Exe = Join-Path $scriptDirectory '..\..\bin_x64\xrEngine.exe'
}
$gameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$config = Join-Path $gameRoot 'fsgame.ltx'
$appData = (Join-Path $gameRoot 'appdata') + '\'
$save = Join-Path $appData 'savedgames\b23_jupiter.scop'

foreach ($path in @($Exe, $config, $save)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Файл отсутствует: $path" }
}
$expectedAlias = '^\$app_data_root\$\s*=\s*true\s*\|\s*false\s*\|\s*' + [regex]::Escape($appData) + '\s*$'
if (-not (Select-String -LiteralPath $config -Pattern $expectedAlias -Quiet)) {
    throw ('Проверьте путь $app_data_root$ в {0}. Ожидается {1}' -f $config, $appData)
}
if (Get-Process -Name xrEngine -ErrorAction SilentlyContinue) { throw 'Закройте текущий экземпляр xrEngine.' }

$arguments = @('-fsltx', 'fsgame.ltx', '-nogameintro', '-log')
if (-not $R4) {
    $arguments += '-dx12_native_game'
    if ($SceneLog) { $arguments += '-SceneLog' }
    if ($LightId -ne 0) {
        $arguments += @('-LightId', $LightId.ToString([Globalization.CultureInfo]::InvariantCulture))
    }
}
$arguments += @('-start', 'server(b23_jupiter/single/alife/load)')

$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = (Resolve-Path -LiteralPath $Exe).Path
$start.WorkingDirectory = $gameRoot
$start.Arguments = $arguments -join ' '
$start.UseShellExecute = $false

$features = @{
    STATIC_SCENE = '1'
    RIGID = '1'
    SKINNED = '1'
    DETAILS = '1'
    LIGHTING = '1'
    SUN = '1'
    LOCAL_LIGHTS = [int](-not $NoLocalLights)
    SHADOWS = [int](-not $NoShadows)
    SUN_SHADOWS = [int](-not $NoShadows -and -not $NoSunShadows)
    TRANSPARENCY = [int](-not $NoTransparency)
    LIT_DECALS = [int](-not $NoLitDecals)
    SOURCE_ALPHA_TEST = [int](-not $LegacyAlpha)
    WATER = [int](-not $NoWater -and -not $NoTransparency)
    PUDDLES = [int](-not $NoPuddles -and -not $NoTransparency)
    DYNAMIC_SUN_SHADOWS = [int](-not $NoDynamicSunShadows -and -not $NoShadows -and -not $NoSunShadows)
    LOCAL_SHADOWS = [int](-not $NoShadows -and -not $NoLocalShadows -and -not $NoLocalLights)
    HEMI = [int](-not $NoHemi)
    TERRAIN_DETAIL = [int](-not $NoTerrainDetail)
    SCENE_LOG = [int]($SceneLog.IsPresent -or $LightId -ne 0 -or $FixedLightIds -or $ProbeFrames -ne 0)
    TRACE_LIGHTS = [int]$SceneLog.IsPresent
    VALIDATION = [int]$Validation.IsPresent
    FIXED_LIGHT_IDS = [string]$FixedLightIds
    FREEZE_LIGHT_RECORD = [int]$FreezeLightRecord.IsPresent
    LIGHT_PROBE_FRAMES = $ProbeFrames
    LIGHT_PROBE_DELAY = $ProbeDelay
    LIGHT_PROBE_ORIGIN = [string]$ProbeOrigin
}
foreach ($feature in $features.GetEnumerator()) {
    $start.EnvironmentVariables['OGSR_DX12_GAME_' + $feature.Key] = [string]$feature.Value
}
$views = @{
    Default = 0; Material = 1; ShadowDepth = 4; ShadowVisibility = 5; ShadowSlot = 6
    Albedo = 7; Normals = 8; Lighting = 9; Hemi = 10; WaterNormals = 11; WaterReflection = 12; LightTerms = 13
    LocalRaw = 14; LocalProbe = 15; LightIndex = 16; WorldPosition = 17
}
$start.EnvironmentVariables['OGSR_DX12_GAME_SCENE_VIEW'] = [string]$views[$View]
$start.EnvironmentVariables.Remove('OGSR_DX12_GAME_TRACE_LIGHT_ID')
if ($LightId -ne 0) {
    $start.EnvironmentVariables['OGSR_DX12_GAME_TRACE_LIGHT_ID'] = [string]$LightId
}
$start.EnvironmentVariables.Remove('OGSR_DX12_GAME_METRICS')
$start.EnvironmentVariables.Remove('OGSR_DX12_NATIVE_CAPTURE_DIR')
$start.EnvironmentVariables.Remove('OGSR_DX12_GAME_R1_VISUAL_ID')
if ($GeometryVisualId) {
    $start.EnvironmentVariables['OGSR_DX12_GAME_R1_VISUAL_ID'] = $GeometryVisualId -join ','
}
$start.EnvironmentVariables.Remove('OGSR_DX12_GAME_R1_OBJECT_ID')
if ($null -ne $GeometryObjectId) {
    $start.EnvironmentVariables['OGSR_DX12_GAME_R1_OBJECT_ID'] = [string]$GeometryObjectId
}
Write-Host ('"{0}" {1}' -f $start.FileName, $start.Arguments)
$engine = [System.Diagnostics.Process]::Start($start)
try {
    $engine.WaitForExit()
    if ($engine.ExitCode -ne 0) { throw "xrEngine завершился с кодом $($engine.ExitCode)." }
}
finally {
    $engine.Dispose()
}
