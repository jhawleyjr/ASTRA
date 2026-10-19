# M7b PR 3's C++ tests of oracle (b) (docs/plans/m7b-pr3-autopilot-peer.md §8): the notional transport's
# sim-side autopilot peer against embedded/autopilot. `ctest -R AutopilotPeer` selects all of them.

# R5, the unit-level equivalence: the peer's law (its header only) against astra_autopilot_lib on scripted and
# seeded frame sequences, under every preset's flags. No astra_core: the law includes only the ICD.
# ../support/alloc_counter.cpp replaces global operator new/delete for this executable, as in
# astra_autopilot_tests, so LawUpdateAllocatesNothing can count.
add_executable(astra_autopilot_peer_equivalence_tests autopilot_peer_equivalence_test.cpp
               ../support/alloc_counter.cpp)
target_link_libraries(astra_autopilot_peer_equivalence_tests PRIVATE
  astra_autopilot_lib astra_build_flags GTest::gtest_main)
gtest_discover_tests(astra_autopilot_peer_equivalence_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS unit)

# The shipped peer's Transport against the autopilot, its config refusals against the autopilot's command
# line, and the sanitizer and zero-allocation smoke of the shipped peer and the variant's four defects in a
# Simulation. It loads plugins by path, so it is registered with astra_add_plugin_test.
add_executable(astra_autopilot_peer_tests autopilot_peer_test.cpp ../support/alloc_counter.cpp)
target_link_libraries(astra_autopilot_peer_tests PRIVATE
  astra_autopilot_lib astra::core GTest::gtest_main)
astra_add_plugin_test(astra_autopilot_peer_tests)
target_compile_definitions(astra_autopilot_peer_tests PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\""
  "ASTRA_SOURCE_DIR=\"${ASTRA_SOURCE_DIR}\"")
gtest_discover_tests(astra_autopilot_peer_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS physics)
