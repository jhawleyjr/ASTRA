# M6 PR 9 (docs/plans/m6-pr9-benchmark.md; M6 plan R19, Q11): the frame-time benchmark.
#
# astra_frame_benchmark times step() for each named config and a logged run end to end, and writes JSON
# only after every frame it timed is checked. It is built in every preset, and its smoke runs in every
# preset: the smoke asserts the fields and the frame counts, never a speed, so ASan and UBSan check it
# too. A baseline is a Release run (`just benchmark <out>`), and the JSON records the build type.
#
# The steady clock it reads lives here, in tests/, outside the wall-clock scan's scope (ADR-0010,
# ADR-0023), like tests/lockstep's timing tests.
add_executable(astra_frame_benchmark frame_benchmark.cpp)
target_link_libraries(astra_frame_benchmark PRIVATE astra::core)
astra_add_plugin_test(astra_frame_benchmark)
target_include_directories(astra_frame_benchmark PRIVATE "${ASTRA_SOURCE_DIR}")
target_compile_definitions(astra_frame_benchmark PRIVATE
  "ASTRA_SOURCE_DIR=\"${ASTRA_SOURCE_DIR}\""
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_BENCHMARK_BUILD_TYPE=\"${CMAKE_BUILD_TYPE}\"")

# D5's statistics against hand-worked samples: fast, label unit.
add_executable(astra_frame_stats_test frame_stats_test.cpp)
target_link_libraries(astra_frame_stats_test PRIVATE GTest::gtest_main astra_build_flags)
target_include_directories(astra_frame_stats_test PRIVATE "${ASTRA_SOURCE_DIR}")
gtest_discover_tests(astra_frame_stats_test DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)

# R19's smoke: runs the benchmark at 1000 frames and checks every field, the frame counts, and that a
# halting config and a bad argument record nothing. Stdlib Python only, like scripts/*.py.
find_program(ASTRA_PYTHON3 NAMES python3 REQUIRED)
add_test(NAME FrameBenchmark.smoke
         COMMAND "${ASTRA_PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/check_frame_benchmark.py"
                 "$<TARGET_FILE:astra_frame_benchmark>" "${ASTRA_SOURCE_DIR}")
set_tests_properties(FrameBenchmark.smoke PROPERTIES LABELS benchmark TIMEOUT 600)
