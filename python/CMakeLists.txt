# astra._core, the nanobind module (docs/architecture.md §12). Added by the top level only under scikit-build-core
# (SKBUILD) or an explicit -DASTRA_BUILD_PYTHON=ON with Python_EXECUTABLE pointing at the venv.
#
# Editable-only and unsanitized by design: nobody ships or imports a prebuilt module (ADR-0009), the installed
# module resolves this build's libastra_core.so by absolute RPATH, and a sanitized extension could not be imported
# by a plain interpreter without LD_PRELOAD. Both are refused at configure so the failure is a message, not a
# broken import later.
if(ASTRA_SANITIZERS)
  message(FATAL_ERROR "astra._core cannot be built with sanitizers (ASTRA_SANITIZERS='${ASTRA_SANITIZERS}'): "
                      "a plain interpreter could not import it. Use `just python` (unsanitized, build/python).")
endif()
if(SKBUILD AND (SKBUILD_STATE STREQUAL "wheel" OR SKBUILD_STATE STREQUAL "sdist"))
  message(FATAL_ERROR "astra is editable-only (ADR-0009: nothing ships a prebuilt module); "
                      "install it with `just python` (pip install -e), not as a ${SKBUILD_STATE}.")
endif()

find_package(Python 3.12 COMPONENTS Interpreter Development.Module REQUIRED)
execute_process(COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT COMMAND_ERROR_IS_FATAL ANY)
find_package(nanobind 3.0 CONFIG REQUIRED)
# M3 PR 5 (docs/plans/pr5-nanobind-bindings.md, "What Config from a Python dict needs"): yaml-cpp
# is already an apt-installed, project-wide dependency (astra_core's own CMakeLists.txt links it
# PRIVATE, which does not propagate here) -- Config.from_dict builds a YAML::Node directly in this
# translation unit and emits it through the existing, unmodified Config::from_string(), so no
# core/ change was needed, only this direct link.
find_package(yaml-cpp REQUIRED)

# astra::core brings astra_build_flags (-Werror everywhere, §14). NB_SUPPRESS_WARNINGS makes the nanobind and
# Python headers SYSTEM includes: Python.h is C-style casts throughout and -Wold-style-cast would reject it.
# NB_DOMAIN keeps this module's type and exception-translator tables apart from any other nanobind extension in
# the process, so a translator registered elsewhere cannot catch an astra::Error first.
nanobind_add_module(_core NB_STATIC NB_SUPPRESS_WARNINGS NB_DOMAIN astra bindings/_core.cpp)
target_link_libraries(_core PRIVATE astra::core yaml-cpp::yaml-cpp)
target_compile_definitions(_core PRIVATE
  "ASTRA_VERSION=\"${PROJECT_VERSION}\""
  "ASTRA_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\"")
# The installed module keeps pointing at this build's libastra_core.so, so one copy lives in the process and the
# plugins the loader dlopens from ASTRA_PLUGIN_DIR resolve their DT_NEEDED against it by SONAME.
set_target_properties(_core PROPERTIES INSTALL_RPATH "${CMAKE_BINARY_DIR}/lib")
install(TARGETS _core LIBRARY DESTINATION astra)

# The stub is generated in the build tree and copied next to __init__.py (gitignored) on every build, so a
# cleaned build tree or a deleted copy is repaired by the next `just python`. That copy is where pyright and the
# LSP resolve astra._core, since the compiled module itself lives in the venv.
set(astra_stub "${CMAKE_CURRENT_BINARY_DIR}/_core.pyi")
nanobind_add_stub(astra_core_stub
  MODULE _core
  PYTHON_PATH $<TARGET_FILE_DIR:_core>
  DEPENDS _core
  OUTPUT "${astra_stub}")
add_custom_target(astra_core_stub_in_tree ALL
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${astra_stub}" "${ASTRA_SOURCE_DIR}/python/astra/_core.pyi"
  DEPENDS "${astra_stub}"
  COMMENT "Placing _core.pyi next to python/astra/__init__.py")
add_dependencies(astra_core_stub_in_tree astra_core_stub)
