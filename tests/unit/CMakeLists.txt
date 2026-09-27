# The GoogleTest unit suite: fast, run by every preset and by the Stop hook (.claude/hooks/stop-check.sh).
add_executable(astra_unit_tests
  aero/aero_math_namespace_isolation_test.cpp
  atmosphere/isa1976_test.cpp
  config/config_test.cpp
  earth/earth_kinematics_test.cpp
  gravity/inverse_square_test.cpp
  gravity/wgs84_j2_test.cpp
  health/health_monitor_test.cpp
  icd/icd_pack_test.cpp
  icd/icd_test.cpp
  kernel/aero_propulsion_seam_test.cpp
  kernel/clock_test.cpp
  kernel/loopback_transport_test.cpp
  kernel/scheduler_test.cpp
  kernel/notional_transport_cruise_config_test.cpp
  kernel/simulation_halt_test.cpp
  kernel/simulation_derivative_test.cpp
  kernel/simulation_input_seam_test.cpp
  kernel/simulation_physical_config_test.cpp
  kernel/simulation_test.cpp
  kernel/simulation_transport_test.cpp
  kernel/state_buffer_test.cpp
  kernel/state_test.cpp
  kernel/vehicle_environment_test.cpp
  kernel/vehicle_state_coupling_test.cpp
  kernel/vehicle_state_test.cpp
  plugin/load_all_test.cpp
  plugin/loader_test.cpp
  signals/hdf5_file_test.cpp
  signals/input_registry_test.cpp
  signals/signal_registry_test.cpp
  util/air_data_test.cpp
  util/geodesy_test.cpp
  util/random_test.cpp
  util/units_test.cpp
  wind/dryden_filter_test.cpp
  wind/dryden_test.cpp
  wind/table_test.cpp
  wind/wind_determinism_test.cpp
  wind/wind_test.cpp
  wrench/nesc_aero_test.cpp)
target_link_libraries(astra_unit_tests PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_unit_tests PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_TEST_CONFIG_DIR=\"${ASTRA_SOURCE_DIR}/tests/configs\""
  "ASTRA_AIRCRAFT_DIR=\"${ASTRA_SOURCE_DIR}/aircraft\"")
# tests/golden/atmosphere/isa1976_table.hpp (human-owned) is included as
# "golden/atmosphere/isa1976_table.hpp" -- the astra_frames_test pattern below.
target_include_directories(astra_unit_tests PRIVATE "${ASTRA_SOURCE_DIR}/tests")
gtest_discover_tests(astra_unit_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)

# M5 PR 2 (docs/plans/m5-tooling.md): LeakSanitizer against the uninstrumented libhdf5, settled with
# no suppression. `clean` must exit 0 with no LSan report; `leak_control` leaks on purpose and must
# be reported, which shows detection is live. ASAN_OPTIONS pins detect_leaks on for both, whatever
# the caller's environment. Sanitizer presets with ASan only; the names carry "Hdf5" for `-R Hdf5`.
if(ASTRA_SANITIZERS MATCHES "address")
  add_executable(astra_hdf5_lsan_probe signals/hdf5_lsan_probe.cpp)
  target_link_libraries(astra_hdf5_lsan_probe PRIVATE astra::core)
  add_test(NAME Hdf5LsanProbe.clean
           COMMAND astra_hdf5_lsan_probe clean "${CMAKE_CURRENT_BINARY_DIR}/hdf5_lsan_probe_clean.h5")
  set_tests_properties(Hdf5LsanProbe.clean PROPERTIES
    LABELS unit ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1" FAIL_REGULAR_EXPRESSION "LeakSanitizer")
  add_test(NAME Hdf5LsanProbe.leak_control
           COMMAND astra_hdf5_lsan_probe leak "${CMAKE_CURRENT_BINARY_DIR}/hdf5_lsan_probe_leak.h5")
  set_tests_properties(Hdf5LsanProbe.leak_control PROPERTIES
    LABELS unit ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1"
    PASS_REGULAR_EXPRESSION "LeakSanitizer: detected memory leaks")
endif()

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

# M3 PR 8 (docs/plans/pr8-cruise-regression.md, D20): the 300 s cruise regression run. Own
# executable so ctest's names carry "notional_transport_cruise_regression" for `-R
# notional_transport_cruise_regression`; labeled physics, not unit -- several 300 s runs are too slow
# for the Stop hook's fast unit subset. Includes tests/support/cruise_regression_run.hpp (shared with
# tests/capture/'s harness) and, once captured, tests/golden/notional_transport/'s frozen header.
add_executable(astra_notional_transport_cruise_regression_test
  kernel/notional_transport_cruise_regression_test.cpp)
target_link_libraries(astra_notional_transport_cruise_regression_test
  PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_notional_transport_cruise_regression_test PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_AIRCRAFT_DIR=\"${ASTRA_SOURCE_DIR}/aircraft\"")
target_include_directories(astra_notional_transport_cruise_regression_test PRIVATE
  "${ASTRA_SOURCE_DIR}/tests" "${ASTRA_SOURCE_DIR}/tests/support")
gtest_discover_tests(astra_notional_transport_cruise_regression_test
                     TEST_PREFIX "notional_transport_cruise_regression."
                     DISCOVERY_MODE PRE_TEST PROPERTIES LABELS physics)

add_executable(astra_template_mass_test mass/template_mass_test.cpp)
target_link_libraries(astra_template_mass_test PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_template_mass_test PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\"")
gtest_discover_tests(astra_template_mass_test TEST_PREFIX "template_mass."
                     DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)

# docs/plans/pr4-notional-aero-turbofan.md, R1/R4: aero_math.hpp/turbofan_math.hpp's own pure
# compute_wrench(), called directly with hand-built Inputs/Coefficients -- no plugin instance, no
# Config, no PluginLoader, hence no compile definitions -- the same "unit-tested with no simulator"
# pattern astra_frames_test above and wind/dryden_filter_test.cpp already demonstrate.
add_executable(astra_notional_transport_aero_math_test aero/notional_transport_aero_math_test.cpp)
target_link_libraries(astra_notional_transport_aero_math_test PRIVATE astra::core GTest::gtest_main)
gtest_discover_tests(astra_notional_transport_aero_math_test TEST_PREFIX "notional_transport_aero_math."
                     DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)

add_executable(astra_notional_transport_turbofan_math_test
  turbofan/notional_transport_turbofan_math_test.cpp)
target_link_libraries(astra_notional_transport_turbofan_math_test PRIVATE astra::core GTest::gtest_main)
gtest_discover_tests(astra_notional_transport_turbofan_math_test
                     TEST_PREFIX "notional_transport_turbofan_math."
                     DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)

# R9: the un-edited aircraft/_template copies of aero/turbofan throw ConfigError on their own
# TODO-zeroed config -- same shape as astra_template_mass_test above.
add_executable(astra_template_aero_test aero/template_aero_test.cpp)
target_link_libraries(astra_template_aero_test PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_template_aero_test PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\"")
gtest_discover_tests(astra_template_aero_test TEST_PREFIX "template_aero."
                     DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)

add_executable(astra_template_turbofan_test turbofan/template_turbofan_test.cpp)
target_link_libraries(astra_template_turbofan_test PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_template_turbofan_test PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\"")
gtest_discover_tests(astra_template_turbofan_test TEST_PREFIX "template_turbofan."
                     DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)
