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

// Any failure in the HDF5 wrapper (core/signals/hdf5_file.hpp, ADR-0005): an HDF5 call that
// failed, or a call the wrapper refuses (a partial row, a wrong element type, a closed object).
// what() names the file and the object inside it ("/rate_1/values") and, for an HDF5 failure,
// HDF5's own outermost and innermost error descriptions.
class Hdf5Error : public Error {
 public:
  Hdf5Error(std::string_view file_path, std::string_view object_path, std::string_view message);

  [[nodiscard]] const std::string& file_path() const noexcept { return file_path_; }
  [[nodiscard]] const std::string& object_path() const noexcept { return object_path_; }

 private:
  std::string file_path_;
  std::string object_path_;
};

// A run-log finalization failure (ADR-0025, ADR-0027): Simulation::close_log() could not complete
// or publish the file. what() names the file and the write that failed ("/summary", "the rows of
// /rate_1", "complete", "the publication"), and carries the underlying message. A close_log() that
// threw never put a file at log.path; a refused publication leaves a .partial holding complete = 1.
class LogError : public Error {
 public:
  LogError(std::string_view file_path, std::string_view write, std::string_view message);

  [[nodiscard]] const std::string& file_path() const noexcept { return file_path_; }
  [[nodiscard]] const std::string& write() const noexcept { return write_; }

 private:
  std::string file_path_;
  std::string write_;
};

}  // namespace astra
