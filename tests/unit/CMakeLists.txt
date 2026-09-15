# The GoogleTest unit suite: fast, run by every preset and by the Stop hook (.claude/hooks/stop-check.sh).
add_executable(astra_unit_tests
  icd/icd_test.cpp
  kernel/state_test.cpp
  plugin/load_all_test.cpp
  plugin/loader_test.cpp)
target_link_libraries(astra_unit_tests PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_unit_tests PRIVATE "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\"")
gtest_discover_tests(astra_unit_tests DISCOVERY_MODE PRE_TEST)

# Separate executables so ctest's names carry "frames"/"integrator" for `-R 'frames|integrator'`
# (PLAN.md M1 PR 1 acceptance); TEST_PREFIX puts the substring at the front of every discovered name.
add_executable(astra_frames_test util/frames_test.cpp)
target_link_libraries(astra_frames_test PRIVATE astra::core GTest::gtest_main)
# tests/golden/frames/adr0001_cases.hpp (human-owned) is included as "golden/frames/adr0001_cases.hpp".
target_include_directories(astra_frames_test PRIVATE "${ASTRA_SOURCE_DIR}/tests")
gtest_discover_tests(astra_frames_test TEST_PREFIX "frames." DISCOVERY_MODE PRE_TEST)

add_executable(astra_integrator_test kernel/integrator_test.cpp)
target_link_libraries(astra_integrator_test PRIVATE astra::core GTest::gtest_main)
gtest_discover_tests(astra_integrator_test TEST_PREFIX "integrator." DISCOVERY_MODE PRE_TEST)
