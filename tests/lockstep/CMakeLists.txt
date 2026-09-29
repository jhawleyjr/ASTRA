# M4 PR 4 (docs/plans/m4-pr4-shm-transport.md): the shm transport plugin, the HAL shm backend, and
# the two-process echo peer. Suites ShmTransport, HalShm, EchoPeer (A4's asan selector) and the
# Release-only EchoPeerStress.

# The echo peer: C++17, linked only against the HAL (and so icd/) -- never astra_core (ADR-0008).
# astra_build_flags carries warnings and sanitizers only (compile/link options, not a library).
add_executable(astra_echo_peer echo_peer.cpp)
set_target_properties(astra_echo_peer PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_link_libraries(astra_echo_peer PRIVATE astra::hal astra_build_flags)

# ADR-0008 boundary, checked on the binary: no NEEDED libastra_core, no undefined astra:: symbol.
add_test(NAME EchoPeer_links_no_astra_core
         COMMAND "${CMAKE_COMMAND}" "-DBINARY=$<TARGET_FILE:astra_echo_peer>"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/check_no_astra_core.cmake")
set_tests_properties(EchoPeer_links_no_astra_core PROPERTIES LABELS lockstep)

# The layout and HAL headers as C11 under two compilers (ADR-0024: C11 and C++17), as icd_c11.c.
foreach(compiler IN ITEMS "gcc;${ASTRA_GCC}" "clang;${ASTRA_CLANG_C}")
  list(GET compiler 0 label)
  list(GET compiler 1 cc)
  add_test(NAME HalShm_layout_c11_${label}
           COMMAND "${cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only
                   "-I${ASTRA_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/shm_layout_c11.c")
  set_tests_properties(HalShm_layout_c11_${label} PROPERTIES LABELS lockstep)
endforeach()

# The link-time test hooks (wraps.cpp, plan Q-4): the unmodified astra_hal, with these three
# symbols redirected inside the test executable only.
set(astra_lockstep_wraps
  "LINKER:--wrap=astra_hal_shm_wait_until" "LINKER:--wrap=fstat" "LINKER:--wrap=mmap")

# astra_lockstep_test(<target> [SERIAL] LABEL <label> SOURCES <...>)
function(astra_lockstep_test target)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "SERIAL" "LABEL" "SOURCES")
  add_executable(${target} ${ARG_SOURCES} shm_test_support.cpp wraps.cpp)
  target_link_libraries(${target} PRIVATE astra::core astra::hal GTest::gtest_main)
  target_link_options(${target} PRIVATE ${astra_lockstep_wraps})
  target_compile_definitions(${target} PRIVATE
    "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
    "ASTRA_ECHO_PEER_PATH=\"$<TARGET_FILE:astra_echo_peer>\"")
  add_dependencies(${target} astra_echo_peer)
  astra_add_plugin_test(${target})
  set(properties LABELS ${ARG_LABEL} TIMEOUT 600)
  if(ARG_SERIAL)
    # Timing bounds (Joe's M4 Q3: 500 ms + max(20 ms, 10 %)) are asserted strictly; running these
    # alone keeps ctest --parallel load out of the wake latency instead of widening the bound (Q-8).
    list(APPEND properties RUN_SERIAL TRUE)
  endif()
  gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST PROPERTIES ${properties})
endfunction()

# ../support/alloc_counter.cpp: HalShm.WaitAndSendAllocateNothing (it replaces global operator
# new/delete for this whole executable, as in astra_physics_tests).
# shm_wait.cpp: the plugin's bounded wait compiled in directly, so
# ShmTransport.WaitUntilLatchesExpiryWhenTheKernelReturnsEarly can call it (the plugin itself is a
# dlopen'd object with hidden symbols). The same source, not a copy.
astra_lockstep_test(astra_lockstep_tests LABEL lockstep
  SOURCES shm_transport_test.cpp hal_shm_test.cpp echo_peer_test.cpp ../support/alloc_counter.cpp
          "${ASTRA_SOURCE_DIR}/plugins/sim/transport/shm/shm_wait.cpp")
astra_lockstep_test(astra_lockstep_timing_tests SERIAL LABEL lockstep SOURCES timing_test.cpp)

# The 1e6-exchange stress test (label stress) is registered ONLY in a Release build -- the release
# preset (Joe's change 2): A4's asan selector '(ShmTransport|HalShm|EchoPeer)' also matches
# "EchoPeerStress", and a million exchanges under ASan would dominate that run.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  astra_lockstep_test(astra_echo_peer_stress_test SERIAL LABEL stress
    SOURCES echo_peer_stress_test.cpp)
endif()

# M4 PR 6 (docs/plans/m4-pr6-lockstep-acceptance.md): the closed loop with the real autopilot, suite
# ClosedLoopAcceptance (A6's selector). PR 4's Child spawns ASTRA_ECHO_PEER_PATH; here it names the
# autopilot binary, as tests/embedded does, so the support files stay untouched. One source, two
# executables: every preset gets the limits test and the shortened runs; the asan preset alone
# (sanitizers on, plugins not static) also gets the full 10,000-exchange run, label acceptance.
function(astra_closed_loop_test target label)
  add_executable(${target} closed_loop_acceptance_test.cpp shm_test_support.cpp wraps.cpp)
  target_link_libraries(${target} PRIVATE astra_autopilot_lib astra::core astra::hal
                                          GTest::gtest_main)
  target_link_options(${target} PRIVATE ${astra_lockstep_wraps})
  target_compile_definitions(${target} PRIVATE ${ARGN}
    "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
    "ASTRA_SOURCE_DIR=\"${ASTRA_SOURCE_DIR}\""
    "ASTRA_ECHO_PEER_PATH=\"$<TARGET_FILE:astra_autopilot>\"")
  add_dependencies(${target} astra_autopilot)
  astra_add_plugin_test(${target})
  gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST PROPERTIES LABELS ${label} TIMEOUT 600)
endfunction()

astra_closed_loop_test(astra_closed_loop_tests lockstep)
if(ASTRA_SANITIZERS AND NOT ASTRA_STATIC_PLUGINS)
  astra_closed_loop_test(astra_closed_loop_full_test acceptance ASTRA_CLOSED_LOOP_FULL_LENGTH)
endif()
