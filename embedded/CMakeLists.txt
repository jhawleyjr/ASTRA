# The embedded side: the reference HAL and the example autopilot (docs/architecture.md §11; ADR-0008).
# A CMake project of its own, C++17 on purpose, that the top-level build pulls in with add_subdirectory
# and that also configures alone: `cmake -S embedded` needs only a C++17 compiler (no Eigen, yaml-cpp,
# GoogleTest, or Python). It links only icd/ and embedded/hal/, never astra_core (ADR-0008).
#
# 3.21 is the floor because PROJECT_IS_TOP_LEVEL needs it; nothing here needs more, and an older embedded
# toolchain is the point of this project, so the tree's 3.28 is deliberately not copied.
cmake_minimum_required(VERSION 3.21)
project(astra_embedded LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
  # Standalone: define what the top-level CMakeLists.txt would have, from the same files. astra_build_flags
  # comes from the module both builds include; astra::icd from the same icd/CMakeLists.txt.
  get_filename_component(ASTRA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
  list(APPEND CMAKE_MODULE_PATH "${ASTRA_SOURCE_DIR}/cmake")
  include(AstraBuildFlags)
  add_subdirectory("${ASTRA_SOURCE_DIR}/icd" icd)
endif()

add_subdirectory(hal)
add_subdirectory(autopilot)
