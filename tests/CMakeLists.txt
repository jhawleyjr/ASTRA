# Tests (docs/architecture.md §16 row M0). GoogleTest from apt; test plugins built by the same helper a
# user's aircraft uses, so the on-ramp is exercised by every build.
find_package(GTest 1.14 REQUIRED CONFIG)
include(GoogleTest)

astra_discover_plugins(tests/plugins)
add_subdirectory(unit)

# The ICD static asserts under a C11 compiler that is not the sim's (ADR-0006): gcc and clang, -std=c11,
# pedantic, warnings as errors. A failing assert is a compile error, which is the test.
find_program(ASTRA_GCC NAMES gcc REQUIRED)
find_program(ASTRA_CLANG_C NAMES clang-18 clang REQUIRED)
foreach(compiler IN ITEMS "gcc;${ASTRA_GCC}" "clang;${ASTRA_CLANG_C}")
  list(GET compiler 0 label)
  list(GET compiler 1 cc)
  add_test(NAME icd_static_asserts_${label}_c11
           COMMAND "${cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only
                   "-I${ASTRA_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/icd/icd_c11.c")
endforeach()

# The static-plugins build must not reference dlopen at all; the shared build must, so the check is known
# to be able to fire.
if(ASTRA_STATIC_PLUGINS)
  set(dlopen_test core_has_no_dlopen)
  set(dlopen_expect absent)
else()
  set(dlopen_test core_uses_dlopen)
  set(dlopen_expect present)
endif()
add_test(NAME ${dlopen_test}
         COMMAND "${CMAKE_COMMAND}" "-DLIB=$<TARGET_FILE:astra_core>" -DSYMBOL=dlopen
                 "-DEXPECT=${dlopen_expect}" -P "${ASTRA_SOURCE_DIR}/cmake/check_undefined_symbol.cmake")
