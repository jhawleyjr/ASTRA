# The reference HAL (docs/architecture.md §11; ADR-0008, ADR-0024): C API (astra_hal.h), shm backend,
# and the region layout header (astra_shm_layout.h) that the sim-side shm plugin also includes.
#
# M4 PR 4 builds this IN-TREE ONLY (plan docs/plans/m4-pr4-shm-transport.md, Q-1): the top-level
# CMakeLists adds this directory directly and this file assumes the in-tree astra::icd and
# astra_build_flags targets. M4 PR 5 adds embedded/CMakeLists.txt (its own project(), C++17), makes
# this file standalone-capable, and replaces the top-level line with add_subdirectory(embedded).
#
# ADR-0008: links only icd/ -- never astra_core. C++17 on purpose, overriding the tree's C++23.
# hal_shm_wait.cpp must stay its own object (hal_shm_internal.h: the tests' --wrap hook).
add_library(astra_hal STATIC shm/hal_shm.cpp shm/hal_shm_wait.cpp)
add_library(astra::hal ALIAS astra_hal)
set_target_properties(astra_hal PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_link_libraries(astra_hal PUBLIC astra::icd PRIVATE astra_build_flags)
