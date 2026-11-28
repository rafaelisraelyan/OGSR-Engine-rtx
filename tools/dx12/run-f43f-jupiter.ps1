[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$GameRoot,
    [string]$Exe,
    [switch]$R4,
    [switch]$NoLitDecals,
    [switch]$ParticleCapture,
    [switch]$LegacyAlpha,
    [switch]$NoWater,
    [switch]$NoPuddles,
    [switch]$NoDynamicSunShadows,
    [switch]$NoSunShadows,
    [switch]$NoLocalShadows,
    [switch]$AdaptiveShadows,
    [uint64]$AdaptiveShadowBudgetBytes = [uint64]::MaxValue,
    [uint64]$AdaptiveShadowReserveBytes = 0,
    [switch]$Preflight,
    [switch]$LegacyLocalPriority,
    [ValidateSet(1,2,3)][uint32]$LocalShadowBanks = 1,
    [switch]$SuppressUnmappedLocalLights,
    [switch]$LegacySunPCF,
    [ValidateSet('Legacy','Performance','Balanced','Quality')][string]$SunShadowProfile = 'Legacy',
    [ValidateRange(1,4)][uint32]$SunCascades,
    [ValidateSet(1024,2048,4096)][uint32]$SunShadowResolution,
    [ValidateRange(10,1000)][uint32]$SunShadowDistance,
    [ValidateSet('Auto','LegacyAuto','Point','Bilinear','Legacy','ReceiverPlane','PCF3','PCF5','FR3','PCF7',
        'Subtexel3','Subtexel5','Subtexel7')]
    [string]$SunShadowFilter = 'Auto',
    [ValidateSet('Performance','Balanced','Quality')][string]$SunFilterQuality,
    [ValidateSet('Auto','Legacy','Subtexel3','Subtexel5','Subtexel7')][string]$LocalShadowFilter = 'Auto',
    [ValidateSet('Performance','Balanced','Quality')][string]$LocalFilterQuality,
    [switch]$NoSunNormalBias,
    [switch]$NoSunConstantBias,
    [switch]$NoSunRasterBias,
    [switch]$NoSunSlopeBias,
    [switch]$LegacySkyHemi,
    [switch]$LegacyWaterSSR,
    [switch]$LegacyFloraBounds,
    [switch]$DetailSunShadows,
    [switch]$DetailShadowValidation,
    [switch]$NoHemi,
    [switch]$NoTerrainDetail,
    [switch]$NoShadows,
    [switch]$NoLocalLights,
    [switch]$NoTransparency,
    [uint32[]]$GeometryVisualId,
    [Nullable[uint32]]$GeometryObjectId,
    [switch]$SceneLog,
    [switch]$Metrics,
    [ValidateSet('A','B','C','D','E','F')][string]$PerformanceMode,
    [switch]$ShadowCache,
    [switch]$StaticShadowCache,
    [switch]$NoShadowCache,
    [switch]$ShadowBatching,
    [switch]$ShadowCasterPreparation,
    [switch]$ClusteredLighting,
    [switch]$NoClusteredLighting,
    [switch]$ShadowPhaseTiming,
    [uint64]$MetricsStart = 1,
    [uint64]$MetricsCount = 480,
    [uint64]$MetricsStride = 1,
    [string]$RunDirectory,
    [switch]$Validation,
    [ValidateSet('serial','parallel','verify')][string]$GR = 'serial',
    [ValidatePattern('^(auto|[0-9]|1[0-6])$')][string]$GRWorkers = 'auto',
    [ValidateRange(32,8192)][uint32]$GRGrain = 256,
    [ValidateSet('legacy','multi','parallel')][string]$CommandLists = 'legacy',
    [uint64]$LightId,
    [string]$FixedLightIds,
    [switch]$FreezeLightRecord,
    [ValidateRange(0,600)][uint32]$ProbeFrames,
    [ValidateRange(0,3600)][uint32]$ProbeDelay,
    [string]$ProbeOrigin,
    [ValidateSet('Default','ShadowDepth','ShadowVisibility','ShadowSlot','Albedo','Normals',
        'Lighting','Hemi','WaterNormals','WaterReflection','Material','LightTerms',
        'LocalRaw','LocalProbe','LightIndex','WorldPosition','ShadowFailureReason','SunVisibility','SunDepthDelta',
        'Ambient','SunDirect','LocalTotal','GeometricNormals','HDRClip',
        'SunShadowUV','SunReceiverDepth','SunStoredDepth','SunBias','SunSlope','SunPCFWeights','SunShadowTexelGrid',
        'CascadeIndex','CascadeCoverage','CascadeBlend','CascadeTexelSize','CascadeDepth','ShadowCasterCount','SunShadowVisibility',
        'BakedHemi','PhysicalShadowAddress','ActualShadowResolution','MaterialClass','SunDiffuse','PrimaryFaceVisibility')]
    [string]$View = 'Default'
)

$ErrorActionPreference = 'Stop'
if ($PerformanceMode) {
    if ($R4) { throw 'Режимы A–F предназначены для native DX12.' }
    $AdaptiveShadows = $PerformanceMode -ne 'A'
    $ShadowCache = $PerformanceMode -in @('C','D','E','F')
    $StaticShadowCache = $PerformanceMode -in @('D','E','F')
    $ShadowBatching = $PerformanceMode -in @('E','F')
    $ShadowCasterPreparation = $PerformanceMode -in @('E','F')
    $ClusteredLighting = $PerformanceMode -eq 'F'
    $Metrics = $true
    $SunShadowProfile = 'Balanced'
    $SunCascades = 3
    $SunShadowResolution = 2048
    $SunShadowDistance = 160
    $LocalShadowBanks = 1
    $GR = 'parallel'
    $GRWorkers = '2'
    $GRGrain = 256
    $CommandLists = 'parallel'
    $AdaptiveShadowBudgetBytes = 512MB
    $AdaptiveShadowReserveBytes = 1GB
}
if ($DetailShadowValidation) {
    if ($R4 -or $NoShadows -or $NoSunShadows) { throw 'DetailShadowValidation требует native DX12 и солнечные тени.' }
    $DetailSunShadows = $true
    $Metrics = $true
}
if ($NoShadowCache) { $ShadowCache = $false; $StaticShadowCache = $false }
if ($NoClusteredLighting) { $ClusteredLighting = $false }
if ($ShadowPhaseTiming) { $Metrics = $true }
$filterTiers = @{ Performance = 'Subtexel3'; Balanced = 'Subtexel5'; Quality = 'Subtexel7' }
if ($SunFilterQuality) {
    if ($SunShadowFilter -ne 'Auto') { throw 'Выберите один способ задать солнечный фильтр.' }
    $SunShadowFilter = $filterTiers[$SunFilterQuality]
}
if ($LocalFilterQuality) {
    if ($LocalShadowFilter -ne 'Auto') { throw 'Выберите один способ задать локальный фильтр.' }
    $LocalShadowFilter = $filterTiers[$LocalFilterQuality]
}
if ($StaticShadowCache -and !$ShadowCache) { throw 'Для StaticShadowCache нужен ShadowCache.' }
if ($AdaptiveShadows -and ($R4 -or $NoShadows -or $NoLocalShadows -or $NoLocalLights)) {
    throw 'AdaptiveShadows требует native DX12 с включёнными локальными тенями.'
}
if ($LegacySunPCF -and $SunShadowFilter -notin @('Auto','Legacy')) {
    throw 'Для -LegacySunPCF выберите Auto или Legacy.'
}
if ($LegacySunPCF) { $SunShadowFilter = 'Legacy' }
if ($FixedLightIds -and $FreezeLightRecord) { throw 'Выберите один режим фиксации света.' }
if (($FixedLightIds -or $FreezeLightRecord) -and $NoLocalLights) { throw 'Для фиксации света требуется включённый local lighting.' }
if (($FreezeLightRecord -or $View -in @('LocalProbe','LightIndex','ShadowFailureReason')) -and $LightId -eq 0) {
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
if (!$Preflight -and (Get-Process -Name xrEngine -ErrorAction SilentlyContinue)) { throw 'Закройте текущий экземпляр xrEngine.' }

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
    PARTICLE_CAPTURE = [int]$ParticleCapture.IsPresent
    GR = $GR.ToLowerInvariant()
    GR_WORKERS = $GRWorkers.ToLowerInvariant()
    GR_GRAIN = $GRGrain
    COMMAND_LISTS = $CommandLists.ToLowerInvariant()
    RIGID = '1'
    SKINNED = '1'
    DETAILS = '1'
    LIGHTING = '1'
    SUN = '1'
    LOCAL_LIGHTS = [int](-not $NoLocalLights)
    SHADOWS = [int](-not $NoShadows)
    SUN_SHADOWS = [int](-not $NoShadows -and -not $NoSunShadows)
    SUN_PROFILE = $SunShadowProfile.ToLowerInvariant()
    SUN_CASCADES = $SunCascades
    SUN_RESOLUTION = $SunShadowResolution
    SUN_DISTANCE = $SunShadowDistance
    SUN_FILTER = $SunShadowFilter.ToLowerInvariant()
    LOCAL_FILTER = $LocalShadowFilter.ToLowerInvariant()
    SHADOW_PHASE_TIMING = [int]$ShadowPhaseTiming.IsPresent
    SUN_BIAS_MASK = 15 - [int]$NoSunNormalBias.IsPresent - 2 * [int]$NoSunConstantBias.IsPresent - 4 * [int]$NoSunRasterBias.IsPresent - 8 * [int]$NoSunSlopeBias.IsPresent
    TRANSPARENCY = [int](-not $NoTransparency)
    LIT_DECALS = [int](-not $NoLitDecals)
    SOURCE_ALPHA_TEST = [int](-not $LegacyAlpha)
    WATER = [int](-not $NoWater -and -not $NoTransparency)
    PUDDLES = [int](-not $NoPuddles -and -not $NoTransparency)
    DYNAMIC_SUN_SHADOWS = [int](-not $NoDynamicSunShadows -and -not $NoShadows -and -not $NoSunShadows)
    LOCAL_SHADOWS = [int](-not $NoShadows -and -not $NoLocalShadows -and -not $NoLocalLights)
    STABLE_LOCAL_PRIORITY = [int](-not $LegacyLocalPriority)
    LOCAL_SHADOW_BANKS = $LocalShadowBanks
    ADAPTIVE_SHADOWS = [int]$AdaptiveShadows.IsPresent
    ADAPTIVE_SHADOW_BUDGET_BYTES = $AdaptiveShadowBudgetBytes.ToString([Globalization.CultureInfo]::InvariantCulture)
    ADAPTIVE_SHADOW_RESERVE_BYTES = $AdaptiveShadowReserveBytes.ToString([Globalization.CultureInfo]::InvariantCulture)
    SUPPRESS_UNMAPPED_LOCAL_LIGHTS = [int]$SuppressUnmappedLocalLights.IsPresent
    RECEIVER_PLANE_SUN = [int](-not $LegacySunPCF)
    LEGACY_SKY_HEMI = [int]$LegacySkyHemi.IsPresent
    LEGACY_WATER_SSR = [int]$LegacyWaterSSR.IsPresent
    LEGACY_FLORA_BOUNDS = [int]$LegacyFloraBounds.IsPresent
    DETAIL_SUN_SHADOWS = [int]($DetailSunShadows -and -not $NoShadows -and -not $NoSunShadows)
    HEMI = [int](-not $NoHemi)
    TERRAIN_DETAIL = [int](-not $NoTerrainDetail)
    SCENE_LOG = [int]($SceneLog.IsPresent -or $LightId -ne 0 -or $FixedLightIds -or $ProbeFrames -ne 0)
    TRACE_LIGHTS = [int]$SceneLog.IsPresent
    VALIDATION = [int]$Validation.IsPresent
    METRICS = [int]$Metrics.IsPresent
    METRICS_START = $MetricsStart
    METRICS_COUNT = $MetricsCount
    METRICS_STRIDE = $MetricsStride
    SHADOW_CACHE = [int]$ShadowCache.IsPresent
    SHADOW_STATIC_CACHE = [int]$StaticShadowCache.IsPresent
    SHADOW_BATCHING = [int]$ShadowBatching.IsPresent
    SHADOW_CASTER_PREPARATION = [int]$ShadowCasterPreparation.IsPresent
    CLUSTERED_LIGHTING = [int]$ClusteredLighting.IsPresent
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
    ShadowFailureReason = 18; SunVisibility = 19; SunDepthDelta = 20
    Ambient = 21; SunDirect = 22; LocalTotal = 23; GeometricNormals = 24; HDRClip = 25
    SunShadowUV = 26; SunReceiverDepth = 27; SunStoredDepth = 28; SunBias = 29; SunSlope = 30
    SunPCFWeights = 31; SunShadowTexelGrid = 32; CascadeIndex = 33; CascadeCoverage = 34
    CascadeBlend = 35; CascadeTexelSize = 36; CascadeDepth = 37; ShadowCasterCount = 38; SunShadowVisibility = 19
    BakedHemi = 39; PhysicalShadowAddress = 40; ActualShadowResolution = 41; MaterialClass = 42; SunDiffuse = 43; PrimaryFaceVisibility = 44
}
$start.EnvironmentVariables['OGSR_DX12_GAME_SCENE_VIEW'] = [string]$views[$View]
$start.EnvironmentVariables.Remove('OGSR_DX12_GAME_TRACE_LIGHT_ID')
if ($LightId -ne 0) {
    $start.EnvironmentVariables['OGSR_DX12_GAME_TRACE_LIGHT_ID'] = [string]$LightId
}
$start.EnvironmentVariables.Remove('OGSR_DX12_NATIVE_CAPTURE_DIR')
$start.EnvironmentVariables.Remove('OGSR_DX12_GAME_R1_VISUAL_ID')
if ($GeometryVisualId) {
    $start.EnvironmentVariables['OGSR_DX12_GAME_R1_VISUAL_ID'] = $GeometryVisualId -join ','
}
$start.EnvironmentVariables.Remove('OGSR_DX12_GAME_R1_OBJECT_ID')
if ($null -ne $GeometryObjectId) {
    $start.EnvironmentVariables['OGSR_DX12_GAME_R1_OBJECT_ID'] = [string]$GeometryObjectId
}
$verifiedArchives = @()
if ($PerformanceMode) {
    $baselineArchives = [ordered]@{
        'levels/levels.db0' = 'D4A24F79BC69AD666470CB067F7C0E14FAA814F4EC66AFD790CE23550F32C788'
        'levels/levels.db1' = 'BF9EF7D57C790905B79EFCA6714FC37AE9DEBE524BC74E58421616634DB76DDB'
        'localization/base_sounds.db' = 'B1B9728B0AAEB26B89ECA14AD6FC4185E89B85AA96CD42500A84A528D6299DE1'
        'localization/xefis_movies.db' = 'CE2242AA1809944CB5918011DB776E09B5B72DE49E3999159A28FA7F36D514A0'
        'localization/xenglish.db' = '1D5874C050B128A3464EF86A9EEF6112EF14AD829C6C582FAB9A4990AE6AF981'
        'mp/mp_pool.db' = '83105212FDDE8EEE9EF0130288FBE0F17133A39C08D5F096F2ADEAD1B1C380BF'
    }
    $verifiedArchives = @(foreach ($entry in $baselineArchives.GetEnumerator()) {
        $path = Join-Path $gameRoot $entry.Key
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Отсутствует архив $path." }
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($hash -ne $entry.Value) { throw "SHA-256 архива $path отличается от baseline Jupiter." }
        [pscustomobject]@{ Path = $entry.Key; SHA256 = $hash }
    })
}
if ($Preflight) {
    Write-Host ('"{0}" {1}' -f $start.FileName, $start.Arguments)
    $profile = Join-Path $appData 'user_ogsr.ltx'
    $archive = Join-Path $gameRoot 'resources/configs.db'
    $resources = @(Get-ChildItem -LiteralPath (Join-Path $gameRoot 'resources') -Filter 'resources.db*' -File |
        Where-Object Length -gt 0)
    foreach ($path in @($profile, $archive, $save)) {
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 0) {
            throw "Не найден непустой файл Jupiter: $path"
        }
    }
    if (!$resources.Count) { throw 'Не найдены непустые архивы resources.db*.' }
    $levelFiles = @(foreach ($relative in 'gamedata/levels/jupiter', 'levels/jupiter') {
        $directory = Join-Path $gameRoot $relative
        if (Test-Path -LiteralPath $directory -PathType Container) {
            Get-ChildItem -LiteralPath $directory -Name -File | ForEach-Object { "$relative/$_" }
        }
    })
    [pscustomobject]@{
        GameRoot = $gameRoot
        Exe = $start.FileName
        Save = $save
        SaveSha256 = (Get-FileHash -LiteralPath $save).Hash
        Profile = $profile
        ProfileSha256 = (Get-FileHash -LiteralPath $profile).Hash
        JupiterOverlayFiles = $levelFiles
        ResourceArchives = @($resources.Name)
        AdaptiveShadows = $AdaptiveShadows.IsPresent
        BudgetBytes = $features.ADAPTIVE_SHADOW_BUDGET_BYTES
        ReserveBytes = $features.ADAPTIVE_SHADOW_RESERVE_BYTES
        Validation = $Validation.IsPresent
        PerformanceMode = $PerformanceMode
        Cache = $ShadowCache.IsPresent
        StaticCache = $StaticShadowCache.IsPresent
        Batching = $ShadowBatching.IsPresent
        CasterPreparation = $ShadowCasterPreparation.IsPresent
        ClusteredLighting = $ClusteredLighting.IsPresent
        VerifiedArchives = $verifiedArchives
    } | ConvertTo-Json -Depth 3
    return
}
$temporaryConfig = $null
$engine = $null
$isolatedAppData = $null
try {
    if ($PerformanceMode -or $RunDirectory) {
        if (!$RunDirectory) { $RunDirectory = Join-Path $PSScriptRoot '../../artifacts/f43-performance-evolution/game-runs' }
        $runId = '{0}-{1}-{2}' -f $PerformanceMode, (Get-Date -Format 'yyyyMMdd-HHmmss'), ([guid]::NewGuid().ToString('N').Substring(0,8))
        $run = New-Item -ItemType Directory -Path (Join-Path $RunDirectory $runId) -Force
        $isolatedAppData = (New-Item -ItemType Directory -Path (Join-Path $run.FullName 'appdata')).FullName + '\'
        $isolatedSaves = New-Item -ItemType Directory -Path (Join-Path $isolatedAppData 'savedgames')
        Copy-Item -LiteralPath (Join-Path $appData 'user_ogsr.ltx') -Destination $isolatedAppData
        Get-ChildItem -LiteralPath (Join-Path $appData 'savedgames') -Filter 'b23_jupiter.*' -File |
            Copy-Item -Destination $isolatedSaves.FullName
        $configName = "f43-performance-$runId.ltx"
        $configPath = Join-Path $gameRoot $configName
        $originalConfig = Get-Content -LiteralPath $config -Raw
        $isolatedConfig = [regex]::Replace($originalConfig, "(?m)$expectedAlias",
            [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $match.Value.Replace($appData, $isolatedAppData) })
        if ($isolatedConfig -eq $originalConfig) { throw 'Не удалось перенаправить appdata в отдельный каталог.' }
        $file = [IO.File]::Open($configPath, [IO.FileMode]::CreateNew)
        $temporaryConfig = $configPath
        try {
            $writer = [IO.StreamWriter]::new($file, [Text.UTF8Encoding]::new($false))
            try { $writer.Write($isolatedConfig) } finally { $writer.Dispose() }
        } finally { $file.Dispose() }
        Copy-Item -LiteralPath $temporaryConfig -Destination $run.FullName
        $start.Arguments = $start.Arguments.Replace('-fsltx fsgame.ltx', "-fsltx $configName")
        [pscustomobject]@{
            Mode = $PerformanceMode
            Exe = $start.FileName
            ExeSHA256 = (Get-FileHash -LiteralPath $start.FileName).Hash
            SaveSHA256 = (Get-FileHash -LiteralPath $save).Hash
            ProfileSHA256 = (Get-FileHash -LiteralPath (Join-Path $appData 'user_ogsr.ltx')).Hash
            Args = $start.Arguments
            Requested = $features
            Archives = $verifiedArchives
        } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $run.FullName 'launch.json') -Encoding UTF8
    }

    Write-Host ('"{0}" {1}' -f $start.FileName, $start.Arguments)
    $engine = [System.Diagnostics.Process]::Start($start)
    $engine.WaitForExit()
    if ($isolatedAppData) {
        $engine.ExitCode | Set-Content -LiteralPath (Join-Path $run.FullName 'engine.exit')
        $logs = @(Get-ChildItem -LiteralPath (Join-Path $isolatedAppData 'logs') -Filter '*.log' -File -ErrorAction SilentlyContinue)
        if ($logs.Count) {
            Select-String -LiteralPath $logs.FullName -Pattern 'ph_dx12_(perf_modes|shadow_preflight|shadow_assignment|shadow_membership|shadow_details|shadow_detail_cascade|shadow_detail_submit):' |
                ForEach-Object Line | Set-Content -LiteralPath (Join-Path $run.FullName 'actual-modes.log')
            $rows = @(Select-String -LiteralPath $logs.FullName -SimpleMatch 'ph_dx12_perf_detail:' | ForEach-Object {
                $fields = [ordered]@{}
                foreach ($match in [regex]::Matches($_.Line, '([a-z0-9_]+)=([^\s]+)')) {
                    $fields[$match.Groups[1].Value] = $match.Groups[2].Value
                }
                [pscustomobject]$fields
            })
            if ($rows.Count) {
                $rows | Export-Csv -LiteralPath (Join-Path $run.FullName 'performance.csv') -NoTypeInformation -Encoding UTF8
                $culture = [Globalization.CultureInfo]::InvariantCulture
                $frameTimes = @($rows | ForEach-Object {
                    $value = [double]::Parse($_.frame_ms, $culture)
                    if ($value -gt 0 -and ![double]::IsInfinity($value)) { $value }
                } | Sort-Object)
                if ($frameTimes.Count) {
                    $p50 = [int][Math]::Ceiling(0.50 * $frameTimes.Count) - 1
                    $p95 = [int][Math]::Ceiling(0.95 * $frameTimes.Count) - 1
                    [ordered]@{
                        Samples = $frameTimes.Count
                        FrameP50Ms = $frameTimes[$p50]
                        FrameP95Ms = $frameTimes[$p95]
                        PercentileMethod = 'nearest rank'
                        Scope = 'Эта сессия. Межсессионное сравнение требует совпадающих интервалов.'
                    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run.FullName 'performance-summary.json') -Encoding UTF8
                }
            }
        }
        Write-Host $run.FullName
    }
    if ($engine.ExitCode -ne 0) { throw "xrEngine завершился с кодом $($engine.ExitCode)." }
}
finally {
    if ($engine) { $engine.Dispose() }
    if ($temporaryConfig) { Remove-Item -LiteralPath $temporaryConfig }
}
