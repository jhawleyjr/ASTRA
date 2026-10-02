# Tests (docs/architecture.md §16 row M0). GoogleTest from apt; test plugins built by the same helper a
# user's aircraft uses, so the on-ramp is exercised by every build. enable_testing(), find_package(GTest) and
# include(GoogleTest) run in the top-level CMakeLists.txt, ahead of plugin discovery.

astra_discover_plugins(tests/plugins)
add_subdirectory(unit)
add_subdirectory(physics)
# M3 PR 8's cruise regression capture harness (EXCLUDE_FROM_ALL; see tests/capture/CMakeLists.txt).
add_subdirectory(capture)
# tests/golden/dynamics/CMakeLists.txt (the R1/R1-Sim capture harness's EXCLUDE_FROM_ALL target) is
# deliberately NOT added here. The capture step it existed for is done (goldens frozen at `ba383ab`,
# docs/plans/pr4a-dynamics-model.md); its source stays under tests/golden/dynamics/ (human-owned,
# untouched by this PR) for provenance, but is no longer a live CMake target -- if it stayed wired
# in, its still-uncompiled source (which references Eom/post_step_renormalize, both retired by this
# PR) would appear in every preset's compile_commands.json and fail `just tidy`, even though the
# target itself is EXCLUDE_FROM_ALL and never actually built by `just check`. A future golden
# re-capture re-adds this subdirectory for the duration of that one authorized step, the same way
# this PR's own capture session did.

# The ICD static asserts under a C11 compiler that is not the sim's (ADR-0006): gcc and clang, -std=c11,
# pedantic, warnings as errors. A failing assert is a compile error, which is the test.
find_program(ASTRA_GCC NAMES gcc REQUIRED)
find_program(ASTRA_CLANG_C NAMES clang-18 clang REQUIRED)
foreach(compiler IN ITEMS "gcc;${ASTRA_GCC}" "clang;${ASTRA_CLANG_C}")
  list(GET compiler 0 label)
  list(GET compiler 1 cc)
  add_test(NAME icd_static_asserts_${label}_c11
           COMMAND "${cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only
                   "-I${ASTRA_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/icd/icd_c11.c")
  set_tests_properties(icd_static_asserts_${label}_c11 PROPERTIES LABELS unit)
endforeach()

# M4 PR 4's shm transport, HAL, and echo-peer tests; after ASTRA_GCC/ASTRA_CLANG_C, which its C11
# layout checks reuse.
add_subdirectory(lockstep)
# M4 PR 5's autopilot tests: unit, process, and ADR-0008 boundary.
add_subdirectory(embedded)
# M6 PR 9's frame-time benchmark and its smoke (docs/plans/m6-pr9-benchmark.md).
add_subdirectory(benchmark)

# The static-plugins build must not reference dlopen at all; the shared build must, so the check is known
# to be able to fire.
if(ASTRA_STATIC_PLUGINS)
  set(dlopen_test core_has_no_dlopen)
  set(dlopen_expect absent)
else()
  set(dlopen_test core_uses_dlopen)
  set(dlopen_expect present)
endif()
add_test(NAME ${dlopen_test}
         COMMAND "${CMAKE_COMMAND}" "-DLIB=$<TARGET_FILE:astra_core>" -DSYMBOL=dlopen
                 "-DEXPECT=${dlopen_expect}" -P "${ASTRA_SOURCE_DIR}/cmake/check_undefined_symbol.cmake")
set_tests_properties(${dlopen_test} PROPERTIES LABELS unit)
