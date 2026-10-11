# This aircraft's own tests (docs/architecture.md §18): one executable, its ctest names, and the plugin
# build order all come from this directory's path, so a copy made by `just new-aircraft` builds and runs
# them with no edit here or anywhere in the framework. astra_add_plugin_test() (cmake/AstraPlugin.cmake)
# is what makes a clean or targeted shared build produce the plugins before the tests dlopen them.
if(NOT ASTRA_BUILD_TESTS)
  return()
endif()

file(RELATIVE_PATH tests_dir "${ASTRA_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")  # aircraft/<name>/tests
get_filename_component(aircraft_dir "${tests_dir}" DIRECTORY)                       # aircraft/<name>
string(REPLACE "/" "_" tests_id "${tests_dir}")                                     # aircraft_<name>_tests

add_executable(astra_${tests_id} mass_test.cpp aero_test.cpp turbofan_test.cpp)
target_link_libraries(astra_${tests_id} PRIVATE astra::core GTest::gtest_main)
target_compile_definitions(astra_${tests_id} PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_THIS_AIRCRAFT=\"${aircraft_dir}\"")
gtest_discover_tests(astra_${tests_id} TEST_PREFIX "${tests_id}." DISCOVERY_MODE PRE_TEST
                     PROPERTIES LABELS unit)
astra_add_plugin_test(astra_${tests_id})
