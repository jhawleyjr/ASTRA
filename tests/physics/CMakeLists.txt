# Physics oracle tests (docs/architecture.md §16 row M1, PLAN.md M1 PR 3): torque-free rigid body,
# ballistic arc, symmetric-top precession, each against a closed form computed independently of the
# EOM under test. Labeled "physics", not "unit": the Stop hook's `ctest -L unit` (fast subset) skips
# these; `just check`'s unlabeled `ctest` still runs them under every preset. cmake/AstraPlugin.cmake's
# astra_finalize_plugins() gives this target (named explicitly there) a build-order dependency on
# every plugin it dlopens by path.
add_executable(astra_physics_tests
  ballistic_test.cpp
  precession_test.cpp
  torque_free_test.cpp)
target_link_libraries(astra_physics_tests PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_physics_tests PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_TEST_CONFIG_DIR=\"${ASTRA_SOURCE_DIR}/tests/configs\"")
gtest_discover_tests(astra_physics_tests TEST_PREFIX "physics." DISCOVERY_MODE PRE_TEST
                     PROPERTIES LABELS physics)
