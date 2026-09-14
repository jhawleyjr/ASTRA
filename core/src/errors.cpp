#include "astra/errors.hpp"

#include <string>

namespace astra {

namespace {

std::string describe(std::string_view path, std::string_view message) {
  std::string text = "plugin '";
  text += path;
  text += "': ";
  text += message;
  return text;
}

}  // namespace

PluginError::PluginError(std::string_view path, std::string_view message)
    : Error(describe(path, message)), plugin_path_(path) {}

PluginApiVersionError::PluginApiVersionError(std::string_view path, std::uint32_t expected_version,
                                             std::uint32_t actual_version)
    : PluginError(path, "core_api_version " + std::to_string(actual_version) +
                            " does not match this core's " + std::to_string(expected_version) +
                            " (stale build directory? rebuild with `just build`)"),
      expected_(expected_version),
      actual_(actual_version) {}

PluginToolchainError::PluginToolchainError(std::string_view path,
                                           std::string_view expected_toolchain,
                                           std::string_view actual_toolchain)
    : PluginError(path, "built with '" + std::string{actual_toolchain} +
                            "' but this core was built with '" + std::string{expected_toolchain} +
                            "' (stale build directory? rebuild with `just build`)"),
      expected_(expected_toolchain),
      actual_(actual_toolchain) {}

}  // namespace astra
