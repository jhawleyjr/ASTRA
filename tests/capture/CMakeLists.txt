# M3 PR 8's cruise regression capture harness (docs/plans/pr8-cruise-regression.md, D20).
# EXCLUDE_FROM_ALL: never built by `just build`/`just check`'s default target -- only
# capture_cruise_regression.sh builds it, explicitly, in a session Joe has armed. It lives outside
# tests/golden/** (unlike tests/golden/dynamics/'s R1 harness) so it can be written and reviewed before
# arming; only its output lands under tests/golden/notional_transport/.
add_executable(capture_cruise_regression EXCLUDE_FROM_ALL capture_cruise_regression.cpp)
target_link_libraries(capture_cruise_regression PRIVATE astra::core)
target_include_directories(capture_cruise_regression PRIVATE "${ASTRA_SOURCE_DIR}/tests/support")
