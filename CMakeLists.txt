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

# Flags every ASTRA target gets (cmake/AstraBuildFlags.cmake, shared with the standalone embedded build).
include(AstraBuildFlags)

find_package(Eigen3 3.4 REQUIRED CONFIG)
find_package(yaml-cpp 0.8 REQUIRED CONFIG)
# The run log's file format (ADR-0005): the C library only, through CMake's FindHDF5 module (Ubuntu's
# libhdf5-dev ships no CMake config). Linked privately by astra_core; only core/signals/hdf5_file.cpp
# includes <hdf5.h>.
find_package(HDF5 1.10 REQUIRED COMPONENTS C)

include(AstraPlugin)
add_subdirectory(icd)
# embedded/ is its own CMake project (ADR-0008): the HAL and the example autopilot. Built here so `just build`
# produces everything; `just embedded-standalone` builds the same directory with no top-level project.
add_subdirectory(embedded)
add_subdirectory(core)
if(SKBUILD OR ASTRA_BUILD_PYTHON)
  add_subdirectory(python)
endif()
if(ASTRA_BUILD_TESTS)
  # Before plugin discovery: an aircraft's own tests/ directory (aircraft/_template/tests) is found by it and
  # calls gtest_discover_tests and links GTest::gtest_main, and a directory added before enable_testing()
  # gets no CTest file.
  enable_testing()
  find_package(GTest 1.14 REQUIRED CONFIG)
  include(GoogleTest)
endif()
astra_discover_plugins(plugins)
astra_discover_plugins(aircraft)
if(ASTRA_BUILD_TESTS)
  add_subdirectory(tests)
endif()
astra_finalize_plugins()
