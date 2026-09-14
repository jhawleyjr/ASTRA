// Typed errors (docs/architecture.md §14). Exceptions are an init-time mechanism only: the
// per-frame path never throws; step-time model errors go through status flags. The config path that
// caused an init error is added to Error when Config exists (a later milestone); the plugin errors
// already name their plugin.
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

}  // namespace astra
