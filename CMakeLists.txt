# ASTRA — one CMake build for the core, every plugin, and the tests (docs/architecture.md §4, ADR-0009).
cmake_minimum_required(VERSION 3.28)
project(ASTRA VERSION 0.1.0 LANGUAGES CXX)

option(ASTRA_STATIC_PLUGINS "Link every plugin into libastra_core and never dlopen (real-time builds)" OFF)
option(ASTRA_BUILD_TESTS "Build the GoogleTest suites and the test plugins" ON)
option(ASTRA_BUILD_PYTHON
  "Build the astra._core nanobind module (scikit-build-core turns this on; needs the venv Python)" OFF)
set(ASTRA_SANITIZERS "" CACHE STRING
    "Comma-separated -fsanitize= list applied to every target, e.g. address,undefined (empty = none)")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++ modules in this tree. CMake >= 3.28 otherwise scans every C++20+ TU under Ninja and adds an
# `@<object>.modmap` argument to each compile_commands.json entry that only a build of that target
# writes, so clang-tidy fails on any TU not yet built -- every EXCLUDE_FROM_ALL target, always.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(ASTRA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(ASTRA_PLUGIN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins")
set(ASTRA_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/include")

# Flags every ASTRA target gets: -Werror everywhere (§14), sanitizers when the preset asks for them.
# Eigen and GoogleTest come in as SYSTEM includes so their warnings stay theirs. No -ffast-math, ever.
add_library(astra_build_flags INTERFACE)
target_compile_options(astra_build_flags INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wnon-virtual-dtor
  -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Wformat=2 -Werror)
if(ASTRA_SANITIZERS)
  target_compile_options(astra_build_flags INTERFACE
    "-fsanitize=${ASTRA_SANITIZERS}" -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(astra_build_flags INTERFACE "-fsanitize=${ASTRA_SANITIZERS}")
endif()

find_package(Eigen3 3.4 REQUIRED CONFIG)
find_package(yaml-cpp 0.8 REQUIRED CONFIG)

include(AstraPlugin)
add_subdirectory(icd)
# The HAL, in-tree only until M4 PR 5 replaces this line with add_subdirectory(embedded) (ADR-0008;
# docs/plans/m4-pr4-shm-transport.md Q-1).
add_subdirectory(embedded/hal)
add_subdirectory(core)
if(SKBUILD OR ASTRA_BUILD_PYTHON)
  add_subdirectory(python)
endif()
astra_discover_plugins(plugins)
astra_discover_plugins(aircraft)
if(ASTRA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
astra_finalize_plugins()
