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

namespace {

std::string describe_config(std::string_view origin, std::string_view config_path,
                            std::string_view message) {
  std::string text = "config";
  if (!origin.empty()) {
    text += " '";
    text += origin;
    text += "'";
  }
  if (!config_path.empty()) {
    text += " at '";
    text += config_path;
    text += "'";
  }
  text += ": ";
  text += message;
  return text;
}

}  // namespace

ConfigError::ConfigError(std::string_view origin, std::string_view config_path,
                         std::string_view message)
    : Error(describe_config(origin, config_path, message)),
      origin_(origin),
      config_path_(config_path) {}

namespace {

std::string describe_hdf5(std::string_view file_path, std::string_view object_path,
                          std::string_view message) {
  std::string text = "hdf5 '";
  text += file_path;
  text += "'";
  if (!object_path.empty()) {
    text += " at '";
    text += object_path;
    text += "'";
  }
  text += ": ";
  text += message;
  return text;
}

}  // namespace

Hdf5Error::Hdf5Error(std::string_view file_path, std::string_view object_path,
                     std::string_view message)
    : Error(describe_hdf5(file_path, object_path, message)),
      file_path_(file_path),
      object_path_(object_path) {}

}  // namespace astra
