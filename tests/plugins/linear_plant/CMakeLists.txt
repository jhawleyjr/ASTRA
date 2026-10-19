astra_add_plugin(SOURCES linear_plant.cpp)

# M7b PR 2's R4 smoke (docs/plans/m7b-pr2-sampled-data.md, §11 item 2 and §12): the plant and
# tests/plugins/state_feedback_peer under every preset, zero allocations over their frames. Its own
# executable, beside the plant to keep PR 2 inside its named paths; ../../support/alloc_counter.cpp
# replaces global operator new/delete for this whole executable, as in astra_physics_tests.
# `ctest -R LinearPlant` selects it.
add_executable(astra_linear_plant_smoke_test linear_plant_smoke_test.cpp
               ../../support/alloc_counter.cpp)
target_link_libraries(astra_linear_plant_smoke_test PRIVATE astra::core GTest::gtest_main)
astra_add_plugin_test(astra_linear_plant_smoke_test)
target_compile_definitions(astra_linear_plant_smoke_test PRIVATE
  "ASTRA_TEST_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\"")
gtest_discover_tests(astra_linear_plant_smoke_test TEST_PREFIX "linear_plant."
                     DISCOVERY_MODE PRE_TEST PROPERTIES LABELS physics)
