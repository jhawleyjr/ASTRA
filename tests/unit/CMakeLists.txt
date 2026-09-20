# The GoogleTest unit suite: fast, run by every preset and by the Stop hook (.claude/hooks/stop-check.sh).
add_executable(astra_unit_tests
  atmosphere/isa1976_test.cpp
  config/config_test.cpp
  earth/earth_kinematics_test.cpp
  gravity/inverse_square_test.cpp
  gravity/wgs84_j2_test.cpp
  health/health_monitor_test.cpp
  icd/icd_test.cpp
  kernel/aero_propulsion_seam_test.cpp
  kernel/clock_test.cpp
  kernel/scheduler_test.cpp
  kernel/notional_transport_cruise_config_test.cpp
  kernel/simulation_halt_test.cpp
  kernel/simulation_input_seam_test.cpp
  kernel/simulation_physical_config_test.cpp
  kernel/simulation_test.cpp
  kernel/state_test.cpp
  kernel/vehicle_state_test.cpp
  plugin/load_all_test.cpp
  plugin/loader_test.cpp
  signals/input_registry_test.cpp
  signals/signal_registry_test.cpp
  util/geodesy_test.cpp
  util/random_test.cpp
  util/units_test.cpp
  wind/dryden_filter_test.cpp
  wind/dryden_test.cpp
  wind/table_test.cpp
  wind/wind_determinism_test.cpp
  wind/wind_test.cpp
  wrench/aero_model_test.cpp
  wrench/nesc_aero_test.cpp
  wrench/propulsion_model_test.cpp)
target_link_libraries(astra_unit_tests PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_unit_tests PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_TEST_CONFIG_DIR=\"${ASTRA_SOURCE_DIR}/tests/configs\""
  "ASTRA_AIRCRAFT_DIR=\"${ASTRA_SOURCE_DIR}/aircraft\"")
# tests/golden/atmosphere/isa1976_table.hpp (human-owned) is included as
# "golden/atmosphere/isa1976_table.hpp" -- the astra_frames_test pattern below.
target_include_directories(astra_unit_tests PRIVATE "${ASTRA_SOURCE_DIR}/tests")
gtest_discover_tests(astra_unit_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)

# Separate executables so ctest's names carry "frames"/"integrator" for `-R 'frames|integrator'`
# (PLAN.md M1 PR 1 acceptance); TEST_PREFIX puts the substring at the front of every discovered name.
add_executable(astra_frames_test util/frames_test.cpp)
target_link_libraries(astra_frames_test PRIVATE astra::core GTest::gtest_main)
# tests/golden/frames/adr0001_cases.hpp (human-owned) is included as "golden/frames/adr0001_cases.hpp".
target_include_directories(astra_frames_test PRIVATE "${ASTRA_SOURCE_DIR}/tests")
gtest_discover_tests(astra_frames_test TEST_PREFIX "frames." DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)

add_executable(astra_integrator_test kernel/integrator_test.cpp)
target_link_libraries(astra_integrator_test PRIVATE astra::core GTest::gtest_main)
gtest_discover_tests(astra_integrator_test TEST_PREFIX "integrator." DISCOVERY_MODE PRE_TEST
                     PROPERTIES LABELS unit)

# aircraft/notional_transport's and aircraft/_template's own physical/mass plugins (PLAN.md M3
# PR 1): own executables so ctest's names carry "notional_transport_mass"/"template_mass" for
# `-R notional_transport_mass` (PR 1's own accept criterion), same TEST_PREFIX shape as
# frames/integrator above.
add_executable(astra_notional_transport_mass_test mass/notional_transport_mass_test.cpp)
target_link_libraries(astra_notional_transport_mass_test PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_notional_transport_mass_test PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_AIRCRAFT_DIR=\"${ASTRA_SOURCE_DIR}/aircraft\"")
gtest_discover_tests(astra_notional_transport_mass_test TEST_PREFIX "notional_transport_mass."
                     DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)

add_executable(astra_template_mass_test mass/template_mass_test.cpp)
target_link_libraries(astra_template_mass_test PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_template_mass_test PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\"")
gtest_discover_tests(astra_template_mass_test TEST_PREFIX "template_mass."
                     DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)
