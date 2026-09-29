# The plugin build helpers (docs/architecture.md §4, ADR-0009).
#
# A plugin directory's CMakeLists.txt is one line:
#
#   astra_add_plugin(SOURCES isa1976.cpp)
#
# The plugin path is the directory relative to the repo root (plugins/physical/atmosphere/isa1976); the
# target and library name replace '/' with '_' (libastra_plugins_physical_atmosphere_isa1976.so). Copying
# aircraft/_template to aircraft/<name> therefore renames every plugin inside it with no edits.
#
# Shared build (default): each plugin is a MODULE library with -fvisibility=hidden written to
# ${ASTRA_PLUGIN_OUTPUT_DIR}; the loader dlopens it by that name at init.
#
# ASTRA_STATIC_PLUGINS=ON: the same sources compile as an OBJECT library with astra_plugin renamed to
# astra_plugin_<id> through a private compile definition (the plugin source is unchanged), and
# astra_finalize_plugins() links every object plus a generated registration table into astra_core. The
# loader looks the path up in that table and never references dlopen.
#
# NO_LOAD_ALL keeps a plugin out of the generated list the load-all test iterates; it is for test plugins
# that exist to be rejected.

define_property(GLOBAL PROPERTY ASTRA_PLUGIN_PATHS
  BRIEF_DOCS "Plugin paths registered by astra_add_plugin" FULL_DOCS "See cmake/AstraPlugin.cmake")
define_property(GLOBAL PROPERTY ASTRA_PLUGIN_TARGETS
  BRIEF_DOCS "Plugin targets registered by astra_add_plugin" FULL_DOCS "See cmake/AstraPlugin.cmake")
define_property(GLOBAL PROPERTY ASTRA_LOAD_ALL_PATHS
  BRIEF_DOCS "Plugin paths the load-all test iterates" FULL_DOCS "See cmake/AstraPlugin.cmake")
define_property(GLOBAL PROPERTY ASTRA_PLUGIN_TEST_TARGETS
  BRIEF_DOCS "Test targets registered by astra_add_plugin_test" FULL_DOCS "See cmake/AstraPlugin.cmake")

function(astra_add_plugin)
  cmake_parse_arguments(PARSE_ARGV 0 ARG "NO_LOAD_ALL" "" "SOURCES")
  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "astra_add_plugin: SOURCES is required (${CMAKE_CURRENT_SOURCE_DIR})")
  endif()
  if(ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "astra_add_plugin: unknown arguments '${ARG_UNPARSED_ARGUMENTS}' "
                        "(${CMAKE_CURRENT_SOURCE_DIR})")
  endif()

  file(RELATIVE_PATH plugin_path "${ASTRA_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
  if(NOT plugin_path MATCHES "^[A-Za-z0-9_]+(/[A-Za-z0-9_]+)+$")
    message(FATAL_ERROR "astra_add_plugin: the plugin directory must be under the repo root, at least two "
                        "levels deep, and use only [A-Za-z0-9_/]: '${plugin_path}'")
  endif()
  string(REPLACE "/" "_" plugin_id "${plugin_path}")
  set(target "astra_${plugin_id}")

  if(ASTRA_STATIC_PLUGINS)
    add_library(${target} OBJECT ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE astra::core_headers)
    target_compile_definitions(${target} PRIVATE "astra_plugin=astra_plugin_${plugin_id}")
  else()
    add_library(${target} MODULE ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE astra::core)
    set_target_properties(${target} PROPERTIES
      OUTPUT_NAME "astra_${plugin_id}"
      PREFIX "lib"
      SUFFIX ".so"
      LIBRARY_OUTPUT_DIRECTORY "${ASTRA_PLUGIN_OUTPUT_DIR}"
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)
  endif()

  set_property(GLOBAL APPEND PROPERTY ASTRA_PLUGIN_PATHS "${plugin_path}")
  set_property(GLOBAL APPEND PROPERTY ASTRA_PLUGIN_TARGETS "${target}")
  if(NOT ARG_NO_LOAD_ALL)
    set_property(GLOBAL APPEND PROPERTY ASTRA_LOAD_ALL_PATHS "${plugin_path}")
  endif()
endfunction()

# A test executable that loads plugins by path declares it with one call in its own CMakeLists.txt,
# after add_executable:
#
#   astra_add_plugin_test(my_aircraft_tests)
#
# In the shared build astra_finalize_plugins() makes the target depend on every plugin, so a targeted or
# clean build cannot run the test before the .so files exist. In the static build the plugins link into
# astra_core, which the test already links, so the call only records the target. Nothing in this file
# lists a test.
#
# Leaving the call out fails configure in every build: astra_finalize_plugins() refuses an executable whose
# own COMPILE_DEFINITIONS carry ASTRA_TEST_PLUGIN_DIR and that is not registered. It sees nothing else, so
# a test that finds its plugins another way (capture_cruise_regression reads the directory from its command
# line), gets the macro from a directory, source file or linked library, or loads them from a support
# library must still be registered by hand.
function(astra_add_plugin_test target)
  if(NOT "${ARGN}" STREQUAL "")
    message(FATAL_ERROR "astra_add_plugin_test: unknown arguments '${ARGN}' (${CMAKE_CURRENT_SOURCE_DIR})")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "astra_add_plugin_test: '${target}' is not a target; call it after add_executable "
                        "(${CMAKE_CURRENT_SOURCE_DIR})")
  endif()
  set_property(GLOBAL APPEND PROPERTY ASTRA_PLUGIN_TEST_TARGETS "${target}")
endfunction()

# Adds every directory under <root> (relative to the repo root) that contains a CMakeLists.txt. A missing
# root is not an error (plugins/ and aircraft/ arrive with M1). A parent directory that also adds its
# children would make CMake fail configure, which is the intended check: discovery is the only add.
function(astra_discover_plugins root)
  file(GLOB_RECURSE lists CONFIGURE_DEPENDS "${ASTRA_SOURCE_DIR}/${root}/*CMakeLists.txt")
  list(SORT lists)
  foreach(cmakelists IN LISTS lists)
    get_filename_component(dir "${cmakelists}" DIRECTORY)
    file(RELATIVE_PATH rel "${ASTRA_SOURCE_DIR}" "${dir}")
    add_subdirectory("${dir}" "${CMAKE_BINARY_DIR}/${rel}")
  endforeach()
endfunction()

# Sets <out> to every build-system target defined in <dir> and, recursively, in its subdirectories. CMake
# keeps no global target list, so astra_finalize_plugins() walks the directory tree from the root.
function(_astra_collect_targets dir out)
  get_property(collected DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(subdir IN LISTS subdirs)
    _astra_collect_targets("${subdir}" subdir_targets)
    list(APPEND collected ${subdir_targets})
  endforeach()
  set(${out} "${collected}" PARENT_SCOPE)
endfunction()

# Called once, last, from the top-level CMakeLists: refuses an unregistered plugin test, writes the load-all
# list for the tests and, in the static build, the registration table that goes into astra_core together
# with every plugin object.
function(astra_finalize_plugins)
  get_property(paths GLOBAL PROPERTY ASTRA_PLUGIN_PATHS)
  get_property(targets GLOBAL PROPERTY ASTRA_PLUGIN_TARGETS)
  get_property(load_all GLOBAL PROPERTY ASTRA_LOAD_ALL_PATHS)
  get_property(test_targets GLOBAL PROPERTY ASTRA_PLUGIN_TEST_TARGETS)

  # An executable compiled with ASTRA_TEST_PLUGIN_DIR loads plugins by path. Unregistered, it passes the
  # default build, which builds every plugin anyway, and races the .so files only in a targeted or clean
  # shared build. Refused here in the static build too, since the same CMakeLists serves both.
  _astra_collect_targets("${ASTRA_SOURCE_DIR}" all_targets)
  set(unregistered "")
  foreach(candidate IN LISTS all_targets)
    get_target_property(type ${candidate} TYPE)
    if(type STREQUAL "EXECUTABLE" AND NOT candidate IN_LIST test_targets)
      get_target_property(definitions ${candidate} COMPILE_DEFINITIONS)
      if(definitions MATCHES "(^|[^A-Za-z0-9_])ASTRA_TEST_PLUGIN_DIR([^A-Za-z0-9_]|$)")
        get_target_property(dir ${candidate} SOURCE_DIR)
        file(RELATIVE_PATH dir "${ASTRA_SOURCE_DIR}" "${dir}")
        string(APPEND unregistered "\n  ${candidate} (${dir})")
      endif()
    endif()
  endforeach()
  if(unregistered)
    message(FATAL_ERROR "astra_finalize_plugins: an executable compiled with ASTRA_TEST_PLUGIN_DIR loads "
                        "plugins by path and must be registered with astra_add_plugin_test(<target>) after "
                        "its add_executable. Not registered:${unregistered}")
  endif()

  list(LENGTH load_all load_all_count)
  set(entries "")
  foreach(path IN LISTS load_all)
    string(APPEND entries "    \"${path}\",\n")
  endforeach()
  file(CONFIGURE OUTPUT "${ASTRA_GENERATED_INCLUDE_DIR}/astra/all_plugins.hpp" @ONLY CONTENT
"// Generated by astra_finalize_plugins() (cmake/AstraPlugin.cmake); do not edit.
// Every plugin registered without NO_LOAD_ALL, for the test that loads each one and checks its descriptor.
#pragma once

#include <array>
#include <string_view>

namespace astra::generated {

inline constexpr std::array<std::string_view, ${load_all_count}> kAllPlugins = {
${entries}};

}  // namespace astra::generated
")

  if(ASTRA_STATIC_PLUGINS)
    list(LENGTH paths count)
    set(declarations "")
    set(rows "")
    foreach(path IN LISTS paths)
      string(REPLACE "/" "_" id "${path}")
      string(APPEND declarations "extern \"C\" const astra::PluginDescriptor* astra_plugin_${id}();\n")
      string(APPEND rows "    {\"${path}\", &astra_plugin_${id}},\n")
    endforeach()
    if(count EQUAL 0)
      set(table "{}")
    else()
      set(table "{{\n${rows}}}")
    endif()
    set(generated_cpp "${CMAKE_BINARY_DIR}/generated/static_plugins.cpp")
    file(CONFIGURE OUTPUT "${generated_cpp}" @ONLY CONTENT
"// Generated by astra_finalize_plugins() (cmake/AstraPlugin.cmake); do not edit.
// ASTRA_STATIC_PLUGINS registration table: every plugin's astra_plugin(), renamed per plugin by the build
// and called directly by the loader (ADR-0009). No dlopen anywhere in this configuration.
#include <array>
#include <span>

#include \"astra/plugin/descriptor.hpp\"
#include \"astra/plugin/static_registry.hpp\"

${declarations}
namespace astra {
namespace {

constexpr std::array<StaticPluginEntry, ${count}> kTable = ${table};

}  // namespace

std::span<const StaticPluginEntry> static_plugin_table() noexcept { return kTable; }

}  // namespace astra
")
    target_sources(astra_core PRIVATE "${generated_cpp}")
    if(targets)
      target_link_libraries(astra_core PRIVATE ${targets})
    endif()
  elseif(targets)
    # Every test binary that dlopens plugins by path needs a build-order dependency on them. Each one
    # opted in with astra_add_plugin_test() in its own directory; the dependency is added here, after
    # every plugin has registered, because a test under aircraft/a/ is declared before aircraft/b/'s
    # plugins exist.
    foreach(test_target IN LISTS test_targets)
      add_dependencies(${test_target} ${targets})
    endforeach()
  endif()
endfunction()
