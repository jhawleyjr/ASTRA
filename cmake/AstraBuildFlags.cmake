# The one definition of astra_build_flags (M4 PR 5, docs/plans/m4-pr5-autopilot.md D1). Included by the
# top-level CMakeLists.txt and by embedded/CMakeLists.txt, so the in-tree and the standalone embedded builds
# get the same warnings and sanitizer hook without a second copy. Guarded: including it twice, or under a
# parent that already defined the target, is a no-op.
if(NOT TARGET astra_build_flags)
  # Flags every ASTRA target gets: -Werror everywhere (§14), sanitizers when the preset asks for them.
  # Eigen and GoogleTest come in as SYSTEM includes so their warnings stay theirs. No -ffast-math, ever.
  add_library(astra_build_flags INTERFACE)
  target_compile_options(astra_build_flags INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wnon-virtual-dtor
    -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Wformat=2 -Werror)
  if(ASTRA_SANITIZERS)
    target_compile_options(astra_build_flags INTERFACE
      "-fsanitize=${ASTRA_SANITIZERS}" -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(astra_build_flags INTERFACE "-fsanitize=${ASTRA_SANITIZERS}")
  endif()
endif()
