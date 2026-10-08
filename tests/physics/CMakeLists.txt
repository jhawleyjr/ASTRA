# Physics oracle tests (docs/architecture.md §16 row M1, PLAN.md M1 PR 3): torque-free rigid body,
# ballistic arc, symmetric-top precession, each against a closed form computed independently of the
# EOM under test. Labeled "physics", not "unit": the Stop hook's `ctest -L unit` (fast subset) skips
# these; `just check`'s unlabeled `ctest` still runs them under every preset. Its
# astra_add_plugin_test() call gives it a build-order dependency on every plugin it dlopens by path.
#
# no_alloc_test.cpp (docs/architecture.md §15's tests/physics/test_no_alloc, PLAN.md M1 PR 4) links
# ../support/alloc_counter.cpp's global operator new/delete replacement into this target only, never
# astra_unit_tests -- see that file's header comment for why the whole set must move together.
#
# nesc/nesc_cases_test.cpp (PLAN.md M2 PR 3b) absorbs PR 1's single-case nesc_case1_test.cpp (deleted):
# all ten NASA/TM-2015-218675 check cases against tests/golden/nesc/'s generated bands. Its own YAML
# configs live under nesc/ (ASTRA_TEST_NESC_CASE_DIR below), not tests/configs/ -- the task's own
# instruction, not the tests/configs/ convention the rest of this target still uses.
# nesc/nesc_f16_bands_test.cpp (M7 PR 8, ADR-0028) checks the eight F-16 band headers' shape, floors
# and initial conditions before any harness flies them.
#
# dynamics/{rigid_body_r1_golden_test.cpp,cruise_end_to_end_r1_golden_test.cpp} (ADR-0021, PLAN.md
# M3 PR 4a): R1/R1-Sim against tests/golden/dynamics/**'s frozen goldens (rigid_body_6dof_r1_golden.hpp
# used directly as capture_r1_golden.cpp's own witness value producer already does;
# ASTRA_TEST_GOLDEN_DIR below names the frozen cruise_end_to_end_r1_fixture.yaml, never the live
# aircraft/notional_transport/configs/cruise.yaml).
add_executable(astra_physics_tests
  ../support/alloc_counter.cpp
  ballistic_test.cpp
  dryden_psd_test.cpp
  dynamics/cruise_end_to_end_r1_golden_test.cpp
  dynamics/rigid_body_r1_golden_test.cpp
  nesc/nesc_cases_test.cpp
  nesc/nesc_f16_bands_test.cpp
  no_alloc_test.cpp
  precession_test.cpp
  round_earth_drop_test.cpp
  torque_free_test.cpp)
target_link_libraries(astra_physics_tests PRIVATE astra::core GTest::gtest_main)
astra_add_plugin_test(astra_physics_tests)
target_compile_definitions(astra_physics_tests PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_TEST_CONFIG_DIR=\"${ASTRA_SOURCE_DIR}/tests/configs\""
  "ASTRA_TEST_NESC_CASE_DIR=\"${ASTRA_SOURCE_DIR}/tests/physics/nesc\""
  "ASTRA_TEST_GOLDEN_DIR=\"${ASTRA_SOURCE_DIR}/tests/golden\""
  "ASTRA_AIRCRAFT_DIR=\"${ASTRA_SOURCE_DIR}/aircraft\"")
# nesc/nesc_cases_test.cpp includes tests/golden/nesc/*.hpp (human-owned) as "golden/nesc/*.hpp", the
# astra_frames_test pattern.
target_include_directories(astra_physics_tests PRIVATE "${ASTRA_SOURCE_DIR}/tests")
# M4 PR 4 (docs/plans/m4-pr4-shm-transport.md, R6): NoAlloc.*WithShmEchoPeer spawns the echo peer
# through tests/lockstep's support code (tests/lockstep/CMakeLists.txt builds astra_echo_peer).
target_sources(astra_physics_tests PRIVATE ../lockstep/shm_test_support.cpp)
target_compile_definitions(astra_physics_tests PRIVATE
  "ASTRA_ECHO_PEER_PATH=\"$<TARGET_FILE:astra_echo_peer>\"")
add_dependencies(astra_physics_tests astra_echo_peer)
gtest_discover_tests(astra_physics_tests TEST_PREFIX "physics." DISCOVERY_MODE PRE_TEST
                     PROPERTIES LABELS physics)

# docs/plans/pr4-notional-aero-turbofan.md, R2/R5: PLAN.md's own PR 4 acceptance line names
# tests/physics/notional_transport/ for the hand-picked-condition tests that "exercise the rebuilt
# aero/propulsion wrench_b() overrides directly" -- own executables, same TEST_PREFIX shape as
# tests/unit/CMakeLists.txt's astra_notional_transport_mass_test, so `ctest -R
# notional_transport_aero_wrench`/`_turbofan_wrench` select them.
add_executable(astra_notional_transport_aero_wrench_test notional_transport/aero_wrench_test.cpp)
target_link_libraries(astra_notional_transport_aero_wrench_test PRIVATE astra::core GTest::gtest_main)
astra_add_plugin_test(astra_notional_transport_aero_wrench_test)
target_compile_definitions(astra_notional_transport_aero_wrench_test PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_AIRCRAFT_DIR=\"${ASTRA_SOURCE_DIR}/aircraft\"")
gtest_discover_tests(astra_notional_transport_aero_wrench_test
                     TEST_PREFIX "notional_transport_aero_wrench."
                     DISCOVERY_MODE PRE_TEST PROPERTIES LABELS physics)

add_executable(astra_notional_transport_turbofan_wrench_test
  notional_transport/turbofan_wrench_test.cpp)
target_link_libraries(astra_notional_transport_turbofan_wrench_test PRIVATE astra::core GTest::gtest_main)
astra_add_plugin_test(astra_notional_transport_turbofan_wrench_test)
target_compile_definitions(astra_notional_transport_turbofan_wrench_test PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_AIRCRAFT_DIR=\"${ASTRA_SOURCE_DIR}/aircraft\"")
gtest_discover_tests(astra_notional_transport_turbofan_wrench_test
                     TEST_PREFIX "notional_transport_turbofan_wrench."
                     DISCOVERY_MODE PRE_TEST PROPERTIES LABELS physics)
