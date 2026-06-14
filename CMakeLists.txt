cmake_minimum_required(VERSION 3.31)
project(Miro)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

include(Warnings)
include(MiroTypeExport)

option(MIRO_UNITY_BUILD "Enable CMake unity build for the Miro library" OFF)
option(MIRO_BUILD_TESTS "Build Miro internal tests" OFF)

# Opt-in C++26 static reflection (P2996). When ON, Miro serializes plain
# aggregates with no reflect() method and no MIRO_REFLECT macro. Requires
# a P2996-capable toolchain (e.g. the Bloomberg clang-p2996 fork); the
# flags below are clang-p2996 spellings. Left OFF by default so ordinary
# C++20 builds on stock compilers are entirely unaffected.
option(MIRO_ENABLE_REFLECTION "Enable C++26 (P2996) structural reflection" OFF)

if (MIRO_ENABLE_REFLECTION)
    set(CMAKE_CXX_STANDARD 26)
endif ()

add_subdirectory(SDKs)
add_subdirectory(Lib)

if (PROJECT_IS_TOP_LEVEL OR MIRO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif ()
