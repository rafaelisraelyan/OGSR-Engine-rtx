cmake_minimum_required(VERSION 3.20)
project(OGSRPhysicsPhysX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT DEFINED PHYSX_ROOT OR PHYSX_ROOT STREQUAL "")
    if(DEFINED ENV{PHYSX_ROOT})
        set(PHYSX_ROOT "$ENV{PHYSX_ROOT}")
    else()
        message(FATAL_ERROR "Set PHYSX_ROOT to the PhysX SDK 'physx' directory or pass -DPHYSX_ROOT=...")
    endif()
endif()

if(NOT DEFINED PHYSX_LIB_DIR OR PHYSX_LIB_DIR STREQUAL "")
    set(PHYSX_LIB_DIR "${PHYSX_ROOT}/bin/win.x86_64.vc143.mt/release")
endif()

if(NOT EXISTS "${PHYSX_ROOT}/include/PxPhysicsAPI.h")
    message(FATAL_ERROR "PhysX headers not found under ${PHYSX_ROOT}/include")
endif()
if(NOT EXISTS "${PHYSX_LIB_DIR}/PhysX_64.lib")
    message(FATAL_ERROR "PhysX release libraries not found under ${PHYSX_LIB_DIR}")
endif()

set(PHYSICS_PORTABLE_BUILD_TESTS OFF CACHE BOOL "Portable tests are built by PhysicsPortable/CMakeLists" FORCE)
set(PHYSICS_PORTABLE_BUILD_TESTS OFF)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../PhysicsPortable" "${CMAKE_BINARY_DIR}/physics_portable")

add_library(physics_physx STATIC
    src/physx_ragdoll_world.cpp
    src/physx_runtime.cpp
)

target_include_directories(physics_physx
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${PHYSX_ROOT}/include
)

target_link_libraries(physics_physx
    PUBLIC physics_portable
    PRIVATE
        "${PHYSX_LIB_DIR}/PhysX_64.lib"
        "${PHYSX_LIB_DIR}/PhysXCommon_64.lib"
        "${PHYSX_LIB_DIR}/PhysXFoundation_64.lib"
        "${PHYSX_LIB_DIR}/PhysXExtensions_static_64.lib"
        "${PHYSX_LIB_DIR}/PhysXCooking_64.lib"
        "${PHYSX_LIB_DIR}/PhysXPvdSDK_static_64.lib"
        "${PHYSX_LIB_DIR}/PhysXTask_static_64.lib"
)

target_compile_features(physics_physx PUBLIC cxx_std_20)
target_compile_definitions(physics_physx PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(physics_physx PRIVATE /W3 /permissive-)
endif()

# ABI v2 foundation: registry and fail-closed loader. This target deliberately
# does not link PhysX so its tests can run without a scene.
add_library(physics_physx_abi_v2 STATIC
    src/physx_ragdoll_registry.cpp
    src/physx_ragdoll_v2_loader.cpp
)
target_include_directories(physics_physx_abi_v2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(physics_physx_abi_v2 PUBLIC cxx_std_20)
target_compile_definitions(physics_physx_abi_v2 PRIVATE
    OGSR_PHYSX2_STATIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(physics_physx_abi_v2 PRIVATE /W4 /permissive-)
endif()

# Optional runtime bridge DLL. It is built with the Release static runtime (/MT)
# and is loaded dynamically by the Debug game only when -physx_runtime is set.
add_library(physics_physx_runtime SHARED
    src/physx_runtime_c.cpp
    src/physx_ragdoll_c.cpp
    src/physx_ragdoll_c_v2.cpp
    src/physx_ragdoll_scene.cpp
    src/physx_ragdoll_instance.cpp
    src/physx_ragdoll_v2_convert.cpp
)
target_link_libraries(physics_physx_runtime PRIVATE physics_physx physics_physx_abi_v2)
target_include_directories(physics_physx_runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PHYSX_ROOT}/include
)
target_compile_features(physics_physx_runtime PRIVATE cxx_std_20)
target_compile_definitions(physics_physx_runtime PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN OGSR_PHYSX_RUNTIME_EXPORTS)
set_target_properties(physics_physx_runtime PROPERTIES OUTPUT_NAME ogsr_physx_runtime PREFIX "")

option(PHYSICS_PHYSX_BUILD_TESTS "Build standalone PhysX ragdoll tests" ON)
if(PHYSICS_PHYSX_BUILD_TESTS)
    enable_testing()

    add_executable(physics_physx_tests
        tests/test_main.cpp
        tests/humanoid_fixture.cpp
    )
    target_link_libraries(physics_physx_tests PRIVATE physics_physx)
    target_include_directories(physics_physx_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(physics_physx_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    set(PHYSX_RUNTIME_DLLS
        "${PHYSX_LIB_DIR}/PhysX_64.dll"
        "${PHYSX_LIB_DIR}/PhysXCommon_64.dll"
        "${PHYSX_LIB_DIR}/PhysXFoundation_64.dll"
        "${PHYSX_LIB_DIR}/PhysXCooking_64.dll"
    )
    add_custom_command(TARGET physics_physx_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PHYSX_RUNTIME_DLLS} "$<TARGET_FILE_DIR:physics_physx_tests>"
        COMMENT "Copy PhysX runtime DLLs beside the test executable"
    )

    add_test(NAME physics_physx_tests COMMAND physics_physx_tests)

    add_executable(physics_physx_ragdoll_abi_tests
        tests/test_ragdoll_c.cpp
    )
    target_link_libraries(physics_physx_ragdoll_abi_tests PRIVATE physics_physx_runtime physics_portable)
    target_include_directories(physics_physx_ragdoll_abi_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../PhysicsPortable/include
    )
    target_compile_definitions(physics_physx_ragdoll_abi_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_dependencies(physics_physx_ragdoll_abi_tests physics_physx_runtime)
    add_custom_command(TARGET physics_physx_ragdoll_abi_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:physics_physx_runtime>" "$<TARGET_FILE_DIR:physics_physx_ragdoll_abi_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PHYSX_RUNTIME_DLLS} "$<TARGET_FILE_DIR:physics_physx_ragdoll_abi_tests>"
        COMMENT "Copy the ragdoll runtime DLL beside the ABI test"
    )
    add_test(NAME physics_physx_ragdoll_abi_tests COMMAND physics_physx_ragdoll_abi_tests)

    add_executable(physics_physx_stability
        tests/test_stability.cpp
    )
    target_link_libraries(physics_physx_stability PRIVATE physics_physx)
    target_include_directories(physics_physx_stability PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(physics_physx_stability PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_custom_command(TARGET physics_physx_stability POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PHYSX_RUNTIME_DLLS} "$<TARGET_FILE_DIR:physics_physx_stability>"
        COMMENT "Copy PhysX runtime DLLs beside the stability test"
    )
    if(DEFINED ENV{OGSR_RAGDOLL_FIXTURE})
        add_test(NAME physics_physx_stability COMMAND physics_physx_stability)
    endif()

    # P3.1-A: C11/C++ header, registry and loader tests.
    enable_language(C)

    add_library(mock_physx_runtime_v1 SHARED tests/mock_v1_runtime.cpp)
    set_target_properties(mock_physx_runtime_v1 PROPERTIES OUTPUT_NAME mock_physx_runtime_v1 PREFIX "")
    target_compile_definitions(mock_physx_runtime_v1 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    add_library(mock_physx_runtime_v2 SHARED tests/mock_v2_runtime.cpp)
    set_target_properties(mock_physx_runtime_v2 PROPERTIES OUTPUT_NAME mock_physx_runtime_v2 PREFIX "")
    target_include_directories(mock_physx_runtime_v2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(mock_physx_runtime_v2 PRIVATE
        OGSR_PHYSX_RUNTIME_EXPORTS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

    add_library(mock_physx_runtime_v2_missing SHARED tests/mock_v2_runtime.cpp)
    set_target_properties(mock_physx_runtime_v2_missing PROPERTIES OUTPUT_NAME mock_physx_runtime_v2_missing PREFIX "")
    target_include_directories(mock_physx_runtime_v2_missing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(mock_physx_runtime_v2_missing PRIVATE
        OGSR_PHYSX_RUNTIME_EXPORTS
        MOCK_V2_MISSING_SCENE_CREATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

    add_executable(physics_physx_v2_header_c11 tests/test_v2_header_c11.c)
    set_target_properties(physics_physx_v2_header_c11 PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(physics_physx_v2_header_c11 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(physics_physx_v2_header_c11 PRIVATE OGSR_PHYSX2_STATIC)
    add_test(NAME physics_physx_v2_header_c11 COMMAND physics_physx_v2_header_c11)

    add_executable(physics_physx_v2_header_cpp tests/test_v2_header_cpp.cpp)
    target_include_directories(physics_physx_v2_header_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(physics_physx_v2_header_cpp PRIVATE OGSR_PHYSX2_STATIC)
    add_test(NAME physics_physx_v2_header_cpp COMMAND physics_physx_v2_header_cpp)

    add_executable(physics_physx_v2_registry_tests tests/test_v2_registry.cpp)
    target_link_libraries(physics_physx_v2_registry_tests PRIVATE physics_physx_abi_v2)
    target_compile_definitions(physics_physx_v2_registry_tests PRIVATE
        OGSR_PHYSX2_STATIC
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    add_test(NAME physics_physx_v2_registry_tests COMMAND physics_physx_v2_registry_tests)

    add_executable(physics_physx_v2_loader_tests tests/test_v2_loader.cpp)
    target_link_libraries(physics_physx_v2_loader_tests PRIVATE physics_physx_abi_v2)
    target_compile_definitions(physics_physx_v2_loader_tests PRIVATE
        OGSR_PHYSX2_STATIC
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    add_test(NAME physics_physx_v2_loader_tests COMMAND physics_physx_v2_loader_tests)
    add_dependencies(physics_physx_v2_loader_tests
        mock_physx_runtime_v1
        mock_physx_runtime_v2
        mock_physx_runtime_v2_missing
    )
    add_custom_command(TARGET physics_physx_v2_loader_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:mock_physx_runtime_v1>" "$<TARGET_FILE_DIR:physics_physx_v2_loader_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:mock_physx_runtime_v2>" "$<TARGET_FILE_DIR:physics_physx_v2_loader_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:mock_physx_runtime_v2_missing>" "$<TARGET_FILE_DIR:physics_physx_v2_loader_tests>"
        COMMENT "Copy mock v1/v2 runtime DLLs beside the v2 loader test"
    )

    add_executable(physics_physx_v2_multi_tests
        tests/test_v2_multi.cpp
        tests/v2_fixture_builder.cpp
    )
    target_link_libraries(physics_physx_v2_multi_tests PRIVATE physics_physx_runtime physics_portable)
    target_include_directories(physics_physx_v2_multi_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../PhysicsPortable/include
    )
    target_compile_definitions(physics_physx_v2_multi_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_dependencies(physics_physx_v2_multi_tests physics_physx_runtime)
    add_custom_command(TARGET physics_physx_v2_multi_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:physics_physx_runtime>" "$<TARGET_FILE_DIR:physics_physx_v2_multi_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PHYSX_RUNTIME_DLLS} "$<TARGET_FILE_DIR:physics_physx_v2_multi_tests>"
        COMMENT "Copy the ragdoll runtime DLL beside the v2 multi test"
    )
    add_test(NAME physics_physx_v2_multi_tests COMMAND physics_physx_v2_multi_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../artifacts/fixtures-p1/ragdoll_sim_default_stalker.json")

    add_executable(physics_physx_v2_benchmark
        tests/bench_v2_multi.cpp
        tests/v2_fixture_builder.cpp
    )
    target_link_libraries(physics_physx_v2_benchmark PRIVATE physics_physx_runtime physics_portable psapi)
    target_include_directories(physics_physx_v2_benchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../PhysicsPortable/include
    )
    target_compile_definitions(physics_physx_v2_benchmark PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_dependencies(physics_physx_v2_benchmark physics_physx_runtime)
    add_custom_command(TARGET physics_physx_v2_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:physics_physx_runtime>" "$<TARGET_FILE_DIR:physics_physx_v2_benchmark>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PHYSX_RUNTIME_DLLS} "$<TARGET_FILE_DIR:physics_physx_v2_benchmark>"
        COMMENT "Copy the ragdoll runtime DLL beside the v2 benchmark"
    )

    add_executable(physics_physx_v2_abi_tests tests/test_v2_abi.cpp)
    target_link_libraries(physics_physx_v2_abi_tests PRIVATE physics_physx_runtime physics_physx_abi_v2)
    target_include_directories(physics_physx_v2_abi_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../PhysicsPortable/include
    )
    target_compile_definitions(physics_physx_v2_abi_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_dependencies(physics_physx_v2_abi_tests physics_physx_runtime)
    add_custom_command(TARGET physics_physx_v2_abi_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:physics_physx_runtime>" "$<TARGET_FILE_DIR:physics_physx_v2_abi_tests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PHYSX_RUNTIME_DLLS} "$<TARGET_FILE_DIR:physics_physx_v2_abi_tests>"
        COMMENT "Copy the ragdoll runtime DLL beside the v2 ABI test"
    )
    add_test(NAME physics_physx_v2_abi_tests COMMAND physics_physx_v2_abi_tests)
endif()
