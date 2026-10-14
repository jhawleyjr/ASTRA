# This aircraft's own tests (docs/architecture.md §18): the executables, their ctest names, and the plugin
# build order all come from this directory's path, as in aircraft/_template/tests. astra_add_plugin_test()
# (cmake/AstraPlugin.cmake) is what makes a clean or targeted shared build produce the plugins before the
# tests dlopen them. The F-16's suites are named F16* so `ctest -R F16` selects them (M7 PR 6b,
# docs/plans/m7-pr6b-f16-aircraft.md).
if(NOT ASTRA_BUILD_TESTS)
  return()
endif()

file(RELATIVE_PATH tests_dir "${ASTRA_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")  # aircraft/<name>/tests
get_filename_component(aircraft_dir "${tests_dir}" DIRECTORY)                       # aircraft/<name>
string(REPLACE "/" "_" tests_id "${tests_dir}")                                     # aircraft_<name>_tests

add_executable(astra_${tests_id} mass_test.cpp aero_test.cpp engine_test.cpp control_test.cpp)
target_link_libraries(astra_${tests_id} PRIVATE astra::core GTest::gtest_main)
# control_test.cpp (M7 PR 11a, docs/plans/m7-pr11a-control-peer.md) reads this aircraft's configs:
# ASTRA_F16_CONFIG_DIR names its configs/ directory.
target_compile_definitions(astra_${tests_id} PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_THIS_AIRCRAFT=\"${aircraft_dir}\""
  "ASTRA_F16_CONFIG_DIR=\"${ASTRA_SOURCE_DIR}/${aircraft_dir}/configs\"")
gtest_discover_tests(astra_${tests_id} TEST_PREFIX "${tests_id}." DISCOVERY_MODE PRE_TEST
                     PROPERTIES LABELS unit)
astra_add_plugin_test(astra_${tests_id})

# F16NoAlloc (docs/architecture.md §15): its own executable, because tests/support/alloc_counter.cpp
# replaces the global operator new/delete for the whole executable it links into (the tests/embedded and
# tests/physics precedent). `ctest -R NoAlloc` selects it.
add_executable(astra_${tests_id}_no_alloc no_alloc_test.cpp
  "${ASTRA_SOURCE_DIR}/tests/support/alloc_counter.cpp")
target_link_libraries(astra_${tests_id}_no_alloc PRIVATE astra::core GTest::gtest_main)
target_include_directories(astra_${tests_id}_no_alloc PRIVATE "${ASTRA_SOURCE_DIR}")
target_compile_definitions(astra_${tests_id}_no_alloc PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_THIS_AIRCRAFT=\"${aircraft_dir}\"")
gtest_discover_tests(astra_${tests_id}_no_alloc TEST_PREFIX "${tests_id}." DISCOVERY_MODE PRE_TEST
                     PROPERTIES LABELS unit)
astra_add_plugin_test(astra_${tests_id}_no_alloc)
