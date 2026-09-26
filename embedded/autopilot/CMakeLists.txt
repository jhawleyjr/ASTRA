# The example autopilot (M4 PR 5, docs/plans/m4-pr5-autopilot.md; ADR-0008). C++17, linked only against icd/
# and embedded/hal/, never astra_core. Built by embedded/CMakeLists.txt, in-tree or standalone.
#
# The control law and the command line are a static library that needs the ICD only, so the unit tests link
# it without the HAL; main.cpp adds the HAL, and is the only file that touches it.
add_library(astra_autopilot_lib STATIC autopilot.cpp cli.cpp)
add_library(astra::autopilot_lib ALIAS astra_autopilot_lib)
set_target_properties(astra_autopilot_lib PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_link_libraries(astra_autopilot_lib PUBLIC astra::icd PRIVATE astra_build_flags)

# main.cpp is its own object library so the boundary test (tests/embedded) can scan exactly that object for
# clock symbols; the executable links its objects, so nothing else changes.
add_library(astra_autopilot_main OBJECT main.cpp)
set_target_properties(astra_autopilot_main PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_link_libraries(astra_autopilot_main PRIVATE astra_autopilot_lib astra::hal astra_build_flags)

add_executable(astra_autopilot $<TARGET_OBJECTS:astra_autopilot_main>)
set_target_properties(astra_autopilot PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF LINKER_LANGUAGE CXX)
target_link_libraries(astra_autopilot PRIVATE astra_autopilot_lib astra::hal astra_build_flags)
