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
# nesc/f16_evaluator_test.cpp (M7 PR 7, docs/plans/m7-pr7-aero-evaluator.md) is the self-check of Q5's
# independent evaluator (nesc/independent_evaluator.hpp), the aero-only channel route PR 9's harness
# reuses: on the F-16, its aero plus its engine equals wrench_total at every sample.
# nesc/nesc_f16_open_loop_test.cpp (M7 PR 9, docs/plans/m7-pr9-open-loop.md) flies NESC cases 11 and
# 12 from aircraft/f16/configs/ (ASTRA_AIRCRAFT_DIR below) against their ADR-0028 bands through
# nesc/f16_flight.hpp, and checks each config's trim residual against Q7's DML derivation.
# nesc/nesc_f16_negative_controls_test.cpp plants that family's negative controls (the plan's Q4).
# nesc/nesc_f16_closed_loop_test.cpp (M7 PR 11a, docs/plans/m7-pr11a-control-peer.md) flies NESC
# cases 13.1 to 13.4 from aircraft/f16/configs/ with the sim-side F16_control peer against their
# ADR-0028 bands, with the plan's per-sample guards, and checks each config's trim residual.
# nesc/nesc_f16_closed_loop_variants_test.cpp flies the same configs' reported variants (the DML's
# hard-coded references, divisor 1, Q27's geometries), asserting no band, and
# nesc/nesc_f16_closed_loop_controls_test.cpp plants that family's negative controls (the plan's Q7,
# as Joe decided after the measurement, 2026-10-13).
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
  nesc/f16_evaluator_test.cpp
  nesc/nesc_cases_test.cpp
  nesc/nesc_f16_bands_test.cpp
  nesc/nesc_f16_closed_loop_controls_test.cpp
  nesc/nesc_f16_closed_loop_test.cpp
  nesc/nesc_f16_closed_loop_variants_test.cpp
  nesc/nesc_f16_negative_controls_test.cpp
  nesc/nesc_f16_open_loop_test.cpp
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

# M7b PR 4's oracle (c), the F-16's mirror and heading symmetry under F16_control
# (docs/plans/m7b-pr4-f16-symmetry.md, section 12). astra_f16_symmetry_tests runs in every preset: the
# parity probe and the trim checks (sections 5 and 6), then the smokes and the variant pins. Its configs are
# built in nesc/f16_symmetry.hpp from committed text, never from a tracked config. Labeled physics, never
# unit, so the Stop hook's `just test-unit` runs none of it (docs/lessons.md, 2026-10-18).
# ../support/alloc_counter.cpp, for the smokes' zero-allocation counts, as in astra_physics_tests.
add_executable(astra_f16_symmetry_tests
  ../support/alloc_counter.cpp
  nesc/f16_symmetry_probe_test.cpp
  nesc/f16_symmetry_smoke_test.cpp)
target_link_libraries(astra_f16_symmetry_tests PRIVATE astra::core GTest::gtest_main)
astra_add_plugin_test(astra_f16_symmetry_tests)
target_compile_definitions(astra_f16_symmetry_tests PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_AIRCRAFT_DIR=\"${ASTRA_SOURCE_DIR}/aircraft\"")
gtest_discover_tests(astra_f16_symmetry_tests TEST_PREFIX "physics." DISCOVERY_MODE PRE_TEST
                     PROPERTIES LABELS physics)

# The oracle's flights (section 12): the variant and shipped pairs, the five controls and heading
# invariance, registered ONLY in a Release build, the release preset (tests/lockstep's stress-test
# precedent): about 1.08 M frames and 2.9 M derivative calls, about 6 s there (section 1). Labeled
# physics, never unit.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_executable(astra_f16_symmetry_flight_tests
    nesc/f16_symmetry_mirror_test.cpp
    nesc/f16_symmetry_heading_test.cpp)
  target_link_libraries(astra_f16_symmetry_flight_tests PRIVATE astra::core GTest::gtest_main)
  astra_add_plugin_test(astra_f16_symmetry_flight_tests)
  target_compile_definitions(astra_f16_symmetry_flight_tests PRIVATE
    "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\"")
  gtest_discover_tests(astra_f16_symmetry_flight_tests TEST_PREFIX "physics." DISCOVERY_MODE PRE_TEST
                       PROPERTIES LABELS physics)
endif()

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
