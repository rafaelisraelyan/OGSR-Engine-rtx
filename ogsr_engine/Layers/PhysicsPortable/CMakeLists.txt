cmake_minimum_required(VERSION 3.20)
project(OGSRPhysicsPortable LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(physics_portable STATIC
    src/ragdoll.cpp
    src/json.cpp
)

target_include_directories(physics_portable PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(physics_portable PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(physics_portable PRIVATE /W4 /permissive-)
endif()

option(PHYSICS_PORTABLE_BUILD_TESTS "Build portable math/contract tests" ON)
if(PHYSICS_PORTABLE_BUILD_TESTS)
    enable_testing()
    add_executable(physics_portable_tests tests/test_main.cpp)
    target_link_libraries(physics_portable_tests PRIVATE physics_portable)
    add_test(NAME physics_portable_tests COMMAND physics_portable_tests)
endif()
