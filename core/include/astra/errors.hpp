// Typed errors (docs/architecture.md §14). Exceptions are an init-time mechanism only: the
// per-frame path never throws; step-time model errors go through status flags. ConfigError names
// the config path that caused an init error (core/config/config.hpp); the plugin errors already
// name their plugin.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace astra {

class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Any failure to load a plugin by its path (ADR-0009). what() and plugin_path() both name the
// plugin.
class PluginError : public Error {
 public:
  PluginError(std::string_view path, std::string_view message);

  [[nodiscard]] const std::string& plugin_path() const noexcept { return plugin_path_; }

 private:
  std::string plugin_path_;
};

// The path is malformed, is not in the static table, or has no library file in the plugin
// directory.
class PluginNotFoundError : public PluginError {
 public:
  using PluginError::PluginError;
};

// The library exists but could not be opened, has no astra_plugin symbol, or returned a null
// descriptor or model.
class PluginLoadError : public PluginError {
 public:
  using PluginError::PluginError;
};

// descriptor.core_api_version differs from the one compiled into libastra_core.
class PluginApiVersionError : public PluginError {
 public:
  PluginApiVersionError(std::string_view path, std::uint32_t expected_version,
                        std::uint32_t actual_version);

  [[nodiscard]] std::uint32_t expected() const noexcept { return expected_; }
  [[nodiscard]] std::uint32_t actual() const noexcept { return actual_; }

 private:
  std::uint32_t expected_;
  std::uint32_t actual_;
};

// descriptor.built_with differs from the toolchain string compiled into libastra_core.
class PluginToolchainError : public PluginError {
 public:
  PluginToolchainError(std::string_view path, std::string_view expected_toolchain,
                       std::string_view actual_toolchain);

  [[nodiscard]] const std::string& expected() const noexcept { return expected_; }
  [[nodiscard]] const std::string& actual() const noexcept { return actual_; }

 private:
  std::string expected_;
  std::string actual_;
};

// A Config (core/config/config.hpp) getter finding a missing key, a value of the wrong type, or a
// malformed document. what() and config_path() both name the dotted path from the document root
// ("vehicle.models.0.plugin"); origin() names the file or fixture the document came from.
class ConfigError : public Error {
 public:
  ConfigError(std::string_view origin, std::string_view config_path, std::string_view message);

  [[nodiscard]] const std::string& origin() const noexcept { return origin_; }
  [[nodiscard]] const std::string& config_path() const noexcept { return config_path_; }

 private:
  std::string origin_;
  std::string config_path_;
};

}  // namespace astra
