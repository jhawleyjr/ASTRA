# The R1/R1-Sim golden capture harness (docs/plans/pr4a-dynamics-model.md, "R1 golden capture").
# EXCLUDE_FROM_ALL: never built by `just build`/`just check`'s default target -- only
# capture_r1_golden.sh builds it explicitly, once, during the separately authorized capture step.
add_executable(capture_r1_golden EXCLUDE_FROM_ALL capture_r1_golden.cpp)
target_link_libraries(capture_r1_golden PRIVATE astra::core)
