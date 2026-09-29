# M3 PR 8's cruise regression capture harness (docs/plans/pr8-cruise-regression.md, D20).
# EXCLUDE_FROM_ALL: never built by `just build`/`just check`'s default target -- only
# capture_cruise_regression.sh builds it, explicitly, in a session Joe has armed. It lives outside
# tests/golden/** (unlike tests/golden/dynamics/'s R1 harness) so it can be written and reviewed before
# arming; only its output lands under tests/golden/notional_transport/.
add_executable(capture_cruise_regression EXCLUDE_FROM_ALL capture_cruise_regression.cpp)
target_link_libraries(capture_cruise_regression PRIVATE astra::core)
# Being EXCLUDE_FROM_ALL does not exempt it: capture_cruise_regression.sh builds only this target, so the
# plugins it loads must come with it.
astra_add_plugin_test(capture_cruise_regression)
target_include_directories(capture_cruise_regression PRIVATE "${ASTRA_SOURCE_DIR}/tests/support")
# Already OFF for every target via the top-level CMAKE_CXX_SCAN_FOR_MODULES; restated here because this
# EXCLUDE_FROM_ALL target is the one `just tidy` sees without ever building (M3 PR 10), so it must never
# depend on a build-time .modmap even if the top-level default changes.
set_target_properties(capture_cruise_regression PROPERTIES CXX_SCAN_FOR_MODULES OFF)
