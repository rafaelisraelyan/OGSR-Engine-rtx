cmake_minimum_required(VERSION 3.21)
project(OGSR_VolumetricCloudsTest LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "OGSRCloudsTest requires Windows 10/11, Visual Studio 2022 and the Windows SDK.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(OGSRCloudsTest
    src/main.cpp
    src/Application.cpp
    src/Common.cpp
    src/ImageIO.cpp
    src/NoiseVolume.cpp
    src/Renderer.cpp
    src/SliceViewer.cpp
    src/Validation.cpp
    src/Window.cpp
)

target_compile_definitions(OGSRCloudsTest PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

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
            COMMENT "Validating HLSL entry points with fxc"
            VERBATIM
        )
        message(STATUS "OGSR clouds: build-time HLSL validation via ${FXC_EXECUTABLE}")
    else()
        message(WARNING "OGSR clouds: fxc.exe not found; build-time HLSL validation is disabled")
    endif()
endif()
