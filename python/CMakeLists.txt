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

# astra::core brings astra_build_flags (-Werror everywhere, §14). NB_SUPPRESS_WARNINGS makes the nanobind and
# Python headers SYSTEM includes: Python.h is C-style casts throughout and -Wold-style-cast would reject it.
nanobind_add_module(_core NB_STATIC NB_SUPPRESS_WARNINGS bindings/_core.cpp)
target_link_libraries(_core PRIVATE astra::core)
target_compile_definitions(_core PRIVATE
  "ASTRA_VERSION=\"${PROJECT_VERSION}\""
  "ASTRA_PLUGIN_DIR=\"${ASTRA_PLUGIN_OUTPUT_DIR}\"")
# The installed module keeps pointing at this build's libastra_core.so, so one copy lives in the process and the
# plugins the loader dlopens from ASTRA_PLUGIN_DIR resolve their DT_NEEDED against it by SONAME.
set_target_properties(_core PROPERTIES INSTALL_RPATH "${CMAKE_BINARY_DIR}/lib")
install(TARGETS _core LIBRARY DESTINATION astra)

# The stub is generated into the source tree next to __init__.py (gitignored): that is where pyright and the LSP
# resolve astra._core, since the compiled module itself lives in the venv.
nanobind_add_stub(astra_core_stub
  MODULE _core
  PYTHON_PATH $<TARGET_FILE_DIR:_core>
  DEPENDS _core
  OUTPUT "${ASTRA_SOURCE_DIR}/python/astra/_core.pyi")
