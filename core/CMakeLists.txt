# libastra_core.so — the framework kernel (docs/architecture.md §4). Knows no aircraft, no hardware.

# The toolchain string every plugin bakes into its descriptor and the loader compares against its own.
# A plugin from an older configure carries an older string and is rejected: that is the stale-build case
# the check exists for (ADR-0009).
string(TOLOWER "${CMAKE_BUILD_TYPE}" astra_build_type)
if(ASTRA_SANITIZERS)
  set(astra_sanitizers "${ASTRA_SANITIZERS}")
else()
  set(astra_sanitizers "none")
endif()
if(ASTRA_STATIC_PLUGINS)
  set(astra_static "on")
else()
  set(astra_static "off")
endif()
set(ASTRA_BUILT_WITH "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} c++${CMAKE_CXX_STANDARD} "
                     "${astra_build_type} sanitizers=${astra_sanitizers} static-plugins=${astra_static}")
string(REPLACE ";" "" ASTRA_BUILT_WITH "${ASTRA_BUILT_WITH}")
configure_file(include/astra/core_api.hpp.in "${ASTRA_GENERATED_INCLUDE_DIR}/astra/core_api.hpp" @ONLY)

# Usage requirements shared by the core, every plugin, and the tests. In the static build a plugin's
# objects become part of astra_core, so plugins link this instead of astra_core.
add_library(astra_core_headers INTERFACE)
add_library(astra::core_headers ALIAS astra_core_headers)
target_include_directories(astra_core_headers INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${ASTRA_GENERATED_INCLUDE_DIR}")
target_compile_features(astra_core_headers INTERFACE cxx_std_23)
target_link_libraries(astra_core_headers INTERFACE Eigen3::Eigen astra::icd astra_build_flags)
if(ASTRA_STATIC_PLUGINS)
  target_compile_definitions(astra_core_headers INTERFACE ASTRA_STATIC_PLUGINS=1)
endif()

add_library(astra_core SHARED
  src/config/config.cpp
  src/core_api.cpp
  src/errors.cpp
  src/interfaces/atmosphere_model.cpp
  src/interfaces/earth_model.cpp
  src/interfaces/gravity_model.cpp
  src/interfaces/mass_properties_model.cpp
  src/interfaces/model.cpp
  src/interfaces/wind_model.cpp
  src/interfaces/wrench_source.cpp
  src/kernel/eom.cpp
  src/kernel/health_monitor.cpp
  src/kernel/simulation.cpp
  src/kernel/vehicle.cpp
  src/plugin/loader.cpp
  src/signals/input_registry.cpp
  src/signals/signal_registry.cpp)
add_library(astra::core ALIAS astra_core)
target_link_libraries(astra_core PUBLIC astra_core_headers)
# Private: Config's yaml-cpp usage is behind a pimpl (core/include/astra/config/config.hpp), so no
# plugin ever needs the yaml-cpp headers.
target_link_libraries(astra_core PRIVATE yaml-cpp::yaml-cpp)
if(NOT ASTRA_STATIC_PLUGINS)
  target_link_libraries(astra_core PRIVATE ${CMAKE_DL_LIBS})
endif()
set_target_properties(astra_core PROPERTIES
  OUTPUT_NAME astra_core
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
