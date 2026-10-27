cmake_minimum_required(VERSION 3.20)
project(OGSRPhysicsAdapterOGSR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PHYSICS_PORTABLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PHYSICS_PORTABLE_BUILD_TESTS OFF)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../PhysicsPortable" "${CMAKE_BINARY_DIR}/physics_portable")

add_library(physics_adapter_ogsr STATIC
    src/capture.cpp
)

target_include_directories(physics_adapter_ogsr PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(physics_adapter_ogsr PUBLIC physics_portable)
target_compile_features(physics_adapter_ogsr PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(physics_adapter_ogsr PRIVATE /W4 /permissive-)
endif()

option(PHYSICS_ADAPTER_OGSR_BUILD_TESTS "Build portable OGSR capture tests" ON)
if(PHYSICS_ADAPTER_OGSR_BUILD_TESTS)
    enable_testing()
    add_executable(physics_adapter_ogsr_tests tests/test_capture.cpp)
    target_link_libraries(physics_adapter_ogsr_tests PRIVATE physics_adapter_ogsr)
    add_test(NAME physics_adapter_ogsr_tests COMMAND physics_adapter_ogsr_tests)

    # Source-invariant regression test for the unchanged legacy Euler layout.
    add_executable(physics_adapter_ogsr_legacy_axes_tests tests/test_legacy_euler_axes.cpp)
    target_compile_features(physics_adapter_ogsr_legacy_axes_tests PRIVATE cxx_std_20)
    add_test(
        NAME physics_adapter_ogsr_legacy_axes_tests
        COMMAND physics_adapter_ogsr_legacy_axes_tests
                "${CMAKE_CURRENT_SOURCE_DIR}/../../xrPhysics/PHJoint.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/ogsr/ogsr_capture.h"
    )
endif()
