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

# Optional runtime bridge DLL. It is built with the Release static runtime (/MT)
# and is loaded dynamically by the Debug game only when -physx_runtime is set.
add_library(physics_physx_runtime SHARED
    src/physx_runtime_c.cpp
    src/physx_ragdoll_c.cpp
)
target_link_libraries(physics_physx_runtime PRIVATE physics_physx)
target_include_directories(physics_physx_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
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
endif()
