# Physics oracle tests (docs/architecture.md §16 row M1, PLAN.md M1 PR 3): torque-free rigid body,
# ballistic arc, symmetric-top precession, each against a closed form computed independently of the
# EOM under test. Labeled "physics", not "unit": the Stop hook's `ctest -L unit` (fast subset) skips
# these; `just check`'s unlabeled `ctest` still runs them under every preset. cmake/AstraPlugin.cmake's
# astra_finalize_plugins() gives this target (named explicitly there) a build-order dependency on
# every plugin it dlopens by path.
#
# no_alloc_test.cpp (docs/architecture.md §15's tests/physics/test_no_alloc, PLAN.md M1 PR 4) links
# ../support/alloc_counter.cpp's global operator new/delete replacement into this target only, never
# astra_unit_tests -- see that file's header comment for why the whole set must move together.
add_executable(astra_physics_tests
  ../support/alloc_counter.cpp
  ballistic_test.cpp
  dryden_psd_test.cpp
  nesc_case1_test.cpp
  no_alloc_test.cpp
  precession_test.cpp
  round_earth_drop_test.cpp
  torque_free_test.cpp)
target_link_libraries(astra_physics_tests PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_physics_tests PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_TEST_CONFIG_DIR=\"${ASTRA_SOURCE_DIR}/tests/configs\"")
# nesc_case1_test.cpp includes tests/golden/nesc/case1_dragless_sphere.hpp (human-owned) as
# "golden/nesc/case1_dragless_sphere.hpp", the astra_frames_test pattern.
target_include_directories(astra_physics_tests PRIVATE "${ASTRA_SOURCE_DIR}/tests")
gtest_discover_tests(astra_physics_tests TEST_PREFIX "physics." DISCOVERY_MODE PRE_TEST
                     PROPERTIES LABELS physics)
