# The GoogleTest unit suite: fast, run by every preset and (from PR 4) by the Stop hook.
add_executable(astra_unit_tests
  icd/icd_test.cpp
  kernel/state_test.cpp
  plugin/load_all_test.cpp
  plugin/loader_test.cpp)
target_link_libraries(astra_unit_tests PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_unit_tests PRIVATE "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\"")
gtest_discover_tests(astra_unit_tests DISCOVERY_MODE PRE_TEST)
