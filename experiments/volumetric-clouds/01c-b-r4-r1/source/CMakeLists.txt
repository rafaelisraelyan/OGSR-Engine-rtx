cmake_minimum_required(VERSION 3.21)
project(OGSR_VolumetricCloudsTest LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "OGSRCloudsTest requires Windows 10/11, Visual Studio 2022 and the Windows SDK.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OGSR_WEATHER_DEVELOPER_TOOLS
    "Build developer-only weather/timeline/debug CLI and hotkeys" ON)
option(OGSR_WEATHER_SHIPPING_CONTROLS
    "Shipping-controls contract build: no developer weather surface" OFF)
option(OGSR_CLOUDS_FAULT_INJECTION
    "Isolated test-only GPU reconfiguration fault injection" OFF)
if(OGSR_WEATHER_SHIPPING_CONTROLS)
    set(OGSR_WEATHER_DEVELOPER_TOOLS OFF CACHE BOOL
        "Developer weather tools are disabled by shipping-controls build" FORCE)
endif()
if(OGSR_WEATHER_SHIPPING_CONTROLS AND OGSR_CLOUDS_FAULT_INJECTION)
    message(FATAL_ERROR "Fault injection is test-only and must not be built into shipping controls")
endif()

add_executable(OGSRCloudsTest
    src/main.cpp
    src/Application.cpp
    src/CloudCompositor.cpp
    src/CloudFaultInjection.cpp
    src/CloudProfiler.cpp
    src/CloudRayMarcher.cpp
    src/Common.cpp
    src/ImageIO.cpp
    src/NoiseVolume.cpp
    src/Renderer.cpp
    src/SliceViewer.cpp
    src/Validation.cpp
    src/WeatherMap.cpp
    src/Window.cpp
)

target_compile_definitions(OGSRCloudsTest PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
if(OGSR_WEATHER_DEVELOPER_TOOLS)
    target_compile_definitions(OGSRCloudsTest PRIVATE
        OGSR_WEATHER_DEVELOPER_TOOLS=1)
endif()
if(OGSR_WEATHER_SHIPPING_CONTROLS)
    target_compile_definitions(OGSRCloudsTest PRIVATE
        OGSR_WEATHER_SHIPPING_CONTROLS=1)
endif()
if(OGSR_CLOUDS_FAULT_INJECTION)
    target_compile_definitions(OGSRCloudsTest PRIVATE
        OGSR_CLOUDS_FAULT_INJECTION=1)
endif()

target_compile_options(OGSRCloudsTest PRIVATE
    /W4
    /permissive-
    /utf-8
    /Zc:__cplusplus
    /EHsc
)

target_link_libraries(OGSRCloudsTest PRIVATE
    d3d12
    dxgi
    dxguid
    d3dcompiler
    user32
    bcrypt
)

# The HLSL sources are loaded at runtime from <exe>/shaders. Keeping them as
# separate files makes shader iteration possible without recompiling the EXE.
add_custom_command(TARGET OGSRCloudsTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:OGSRCloudsTest>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:OGSRCloudsTest>/shaders"
    COMMENT "Copying volumetric cloud shaders next to OGSRCloudsTest.exe"
    VERBATIM
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT OGSRCloudsTest)

enable_testing()
add_executable(OGSRCloudsAuditTests tests/audit_policy_test.cpp)
target_include_directories(OGSRCloudsAuditTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(OGSRCloudsAuditTests PRIVATE cxx_std_17)
target_compile_options(OGSRCloudsAuditTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
add_test(NAME OGSRCloudsAuditTests COMMAND OGSRCloudsAuditTests)
set_property(TARGET OGSRCloudsAuditTests PROPERTY FOLDER "Tests")

add_executable(OGSRCloudsWeatherTimelineTests tests/weather_timeline_test.cpp)
target_include_directories(OGSRCloudsWeatherTimelineTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(OGSRCloudsWeatherTimelineTests PRIVATE cxx_std_17)
target_compile_options(OGSRCloudsWeatherTimelineTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
add_test(NAME OGSRCloudsWeatherTimelineTests COMMAND OGSRCloudsWeatherTimelineTests)
set_property(TARGET OGSRCloudsWeatherTimelineTests PROPERTY FOLDER "Tests")

add_executable(OGSRCloudsCloudSettingsTests tests/cloud_settings_test.cpp)
target_include_directories(OGSRCloudsCloudSettingsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(OGSRCloudsCloudSettingsTests PRIVATE cxx_std_17)
target_compile_options(OGSRCloudsCloudSettingsTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
add_test(NAME OGSRCloudsCloudSettingsTests COMMAND OGSRCloudsCloudSettingsTests)
set_property(TARGET OGSRCloudsCloudSettingsTests PROPERTY FOLDER "Tests")

add_executable(OGSRCloudsCloudGraphicsStateTests tests/cloud_graphics_state_test.cpp)
target_include_directories(OGSRCloudsCloudGraphicsStateTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(OGSRCloudsCloudGraphicsStateTests PRIVATE cxx_std_17)
target_compile_options(OGSRCloudsCloudGraphicsStateTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
add_test(NAME OGSRCloudsCloudGraphicsStateTests COMMAND OGSRCloudsCloudGraphicsStateTests)
set_property(TARGET OGSRCloudsCloudGraphicsStateTests PROPERTY FOLDER "Tests")

if(OGSR_CLOUDS_FAULT_INJECTION)
    add_executable(OGSRCloudsTerminalHarness tests/terminal_witness_harness.cpp)
    target_include_directories(OGSRCloudsTerminalHarness PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_features(OGSRCloudsTerminalHarness PRIVATE cxx_std_17)
    target_compile_options(OGSRCloudsTerminalHarness PRIVATE
        /W4 /permissive- /utf-8 /EHsc)
    set_property(TARGET OGSRCloudsTerminalHarness PROPERTY FOLDER "Tests")
endif()

# Optional build-time HLSL validation. Runtime compilation remains the source of
# truth (the EXE needs the .hlsl files anyway), but fxc catches syntax errors
# during the build and proves that every entry point compiles for its profile.
option(OGSR_CLOUDS_VALIDATE_SHADERS "Compile HLSL with fxc during the build" ON)
if(OGSR_CLOUDS_VALIDATE_SHADERS)
    find_program(FXC_EXECUTABLE NAMES fxc
        HINTS "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
        PATHS "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        NO_DEFAULT_PATH)
    if(NOT FXC_EXECUTABLE)
        file(GLOB FXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe")
        if(FXC_CANDIDATES)
            list(SORT FXC_CANDIDATES)
            list(GET FXC_CANDIDATES -1 FXC_EXECUTABLE)
        endif()
    endif()

    if(FXC_EXECUTABLE)
        set(SHADER_CHECK_DIR "${CMAKE_CURRENT_BINARY_DIR}/shader_validation")
        add_custom_command(TARGET OGSRCloudsTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_CHECK_DIR}"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E GenerateNoise
                    /Fo "${SHADER_CHECK_DIR}/NoiseGenerate.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/NoiseGenerate.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E ProbePeriodicity
                    /Fo "${SHADER_CHECK_DIR}/NoiseGenerate.probe.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/NoiseGenerate.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E ProbeSamplerWrap
                    /Fo "${SHADER_CHECK_DIR}/NoiseGenerate.sampler.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/NoiseGenerate.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T vs_5_1 /E FullscreenVS
                    /Fo "${SHADER_CHECK_DIR}/SliceViewer.vs.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SliceViewer.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T ps_5_1 /E SlicePS
                    /Fo "${SHADER_CHECK_DIR}/SliceViewer.ps.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SliceViewer.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarch.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarch.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchTransition.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchTransition.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchTransitionShadowOD.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchTransitionShadowOD.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchTransitionShadowInner.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchTransitionShadowInner.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchProbe.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchProbe.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchProbeFormulaF1.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchProbeFormulaF1.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchProbeFormulaF2.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchProbeFormulaF2.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchProbeFormulaF3.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchProbeFormulaF3.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchProbeFormulaF4.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchProbeFormulaF4.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchProbeFormulaF5.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchProbeFormulaF5.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchProbePrototypeA.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchProbePrototypeA.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchProbePrototypeB.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchProbePrototypeB.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchReference.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchReference.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchReferenceA03a.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchReferenceA03a.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchProbeReference.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchProbeReference.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchProbeReferenceA03a.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchProbeReferenceA03a.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchReferenceA03b.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchReferenceA03b.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchProbeReferenceA03b.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchProbeReferenceA03b.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E DensityVolume
                    /Fo "${SHADER_CHECK_DIR}/CloudDensityVolume.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudDensityVolume.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E DensityVolume
                    /Fo "${SHADER_CHECK_DIR}/CloudDensityVolumeA03a.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudDensityVolumeA03a.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E StormBoundary
                    /Fo "${SHADER_CHECK_DIR}/CloudDensityBoundaryA03a.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudDensityVolumeA03a.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E DensityColumns
                    /Fo "${SHADER_CHECK_DIR}/CloudDensityColumnsA03a.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudDensityVolumeA03a.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E DensityVolume
                    /Fo "${SHADER_CHECK_DIR}/CloudDensityVolumeA03b.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudDensityVolumeA03b.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E StormBoundary
                    /Fo "${SHADER_CHECK_DIR}/CloudDensityBoundaryA03b.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudDensityVolumeA03b.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E DensityColumns
                    /Fo "${SHADER_CHECK_DIR}/CloudDensityColumnsA03b.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudDensityVolumeA03b.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E StormBoundary
                    /Fo "${SHADER_CHECK_DIR}/CloudDensityBoundary.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudDensityVolume.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E DensityColumns
                    /Fo "${SHADER_CHECK_DIR}/CloudDensityColumns.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudDensityVolume.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchPrototypeA.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchPrototypeA.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchPrototypeB.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchPrototypeB.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchFormulaF2.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchFormulaF2.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchFormulaF1.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchFormulaF1.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchFormulaF3.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchFormulaF3.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchFormulaF4.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchFormulaF4.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E CloudRayMarch
                    /Fo "${SHADER_CHECK_DIR}/CloudRayMarchFormulaF5.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudRayMarchFormulaF5.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E GenerateWeather
                    /Fo "${SHADER_CHECK_DIR}/WeatherGenerate.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/WeatherGenerate.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T cs_5_1 /E ProbeWeatherSampler
                    /Fo "${SHADER_CHECK_DIR}/WeatherGenerate.probe.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/WeatherGenerate.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T vs_5_1 /E FullscreenVS
                    /Fo "${SHADER_CHECK_DIR}/CloudComposite.vs.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudComposite.hlsl"
            COMMAND "${FXC_EXECUTABLE}" /nologo /Ges /T ps_5_1 /E CloudCompositePS
                    /Fo "${SHADER_CHECK_DIR}/CloudComposite.ps.cso"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/CloudComposite.hlsl"
            COMMENT "Validating HLSL entry points with fxc"
            VERBATIM
        )
        message(STATUS "OGSR clouds: build-time HLSL validation via ${FXC_EXECUTABLE}")
    else()
        message(WARNING "OGSR clouds: fxc.exe not found; build-time HLSL validation is disabled")
    endif()
endif()
