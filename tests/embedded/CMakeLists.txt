# M4 PR 5 (docs/plans/m4-pr5-autopilot.md): the example autopilot's tests. Suites Autopilot, AutopilotCli,
# AutopilotProcess and EmbeddedBoundary (A5's asan selector '(Autopilot|EmbeddedBoundary)').

# R5: the control law and the command line on scripted frames. icd/ only: no HAL, no astra_core.
# ../support/alloc_counter.cpp replaces global operator new/delete for this executable, so
# Autopilot.UpdateAllocatesNothing can count (as in astra_physics_tests and the HAL tests).
add_executable(astra_autopilot_tests autopilot_test.cpp ../support/alloc_counter.cpp)
target_link_libraries(astra_autopilot_tests PRIVATE astra_autopilot_lib astra_build_flags GTest::gtest_main)
gtest_discover_tests(astra_autopilot_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)

# R6: the real astra_autopilot binary against a test-side sim (Creator, shm_test_support). Support sources are
# PR 4's, compiled again here and not modified. ASTRA_ECHO_PEER_PATH is the name PR 4's Child helper spawns;
# here it names the autopilot binary (a naming quirk, so PR 4's files stay untouched).
set(astra_lockstep_dir "${CMAKE_CURRENT_SOURCE_DIR}/../lockstep")
add_executable(astra_autopilot_process_tests autopilot_process_test.cpp
  "${astra_lockstep_dir}/shm_test_support.cpp" "${astra_lockstep_dir}/wraps.cpp")
target_link_libraries(astra_autopilot_process_tests PRIVATE
  astra_autopilot_lib astra::core astra::hal GTest::gtest_main)
target_link_options(astra_autopilot_process_tests PRIVATE
  "LINKER:--wrap=astra_hal_shm_wait_until" "LINKER:--wrap=fstat" "LINKER:--wrap=mmap")
target_compile_definitions(astra_autopilot_process_tests PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_ECHO_PEER_PATH=\"$<TARGET_FILE:astra_autopilot>\"")
add_dependencies(astra_autopilot_process_tests astra_autopilot)
gtest_discover_tests(astra_autopilot_process_tests DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS lockstep TIMEOUT 600)

# R4: ADR-0008's boundary on the built binary and objects. No NEEDED libastra_core and no undefined astra::
# symbol (the script PR 4 wrote for the echo peer, unmodified); no clock anywhere in the autopilot's own
# objects; and the include closure of its sources is embedded/ and icd/.
add_test(NAME EmbeddedBoundary_autopilot_links_no_astra_core
         COMMAND "${CMAKE_COMMAND}" "-DBINARY=$<TARGET_FILE:astra_autopilot>"
                 -P "${astra_lockstep_dir}/check_no_astra_core.cmake")
add_test(NAME EmbeddedBoundary_autopilot_reads_no_clock
         COMMAND "${CMAKE_COMMAND}"
                 "-DOBJECTS=$<TARGET_FILE:astra_autopilot_lib>;$<TARGET_OBJECTS:astra_autopilot_main>"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/check_no_clock.cmake")
add_test(NAME EmbeddedBoundary_autopilot_include_closure
         COMMAND "${CMAKE_COMMAND}" "-DCXX=${CMAKE_CXX_COMPILER}" "-DROOT=${ASTRA_SOURCE_DIR}"
                 "-DSOURCES=${ASTRA_SOURCE_DIR}/embedded/autopilot/autopilot.cpp;${ASTRA_SOURCE_DIR}/embedded/autopilot/cli.cpp;${ASTRA_SOURCE_DIR}/embedded/autopilot/main.cpp"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/check_include_closure.cmake")
set_tests_properties(EmbeddedBoundary_autopilot_links_no_astra_core
                     EmbeddedBoundary_autopilot_reads_no_clock
                     EmbeddedBoundary_autopilot_include_closure PROPERTIES LABELS unit)
