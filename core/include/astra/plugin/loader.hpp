// Loads plugins by path at init (docs/architecture.md §4, ADR-0009).
#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "astra/interfaces/model.hpp"
#include "astra/plugin/descriptor.hpp"

namespace astra {

struct LoadedPlugin {
  const PluginDescriptor* descriptor;
  std::unique_ptr<Model> model;
};

// One plugin a loader has loaded (ADR-0025's /plugins provenance): the config's plugin path, its
// descriptor, and the library file it came from (empty under ASTRA_STATIC_PLUGINS).
struct PluginLoadRecord {
  std::string plugin_path;
  const PluginDescriptor* descriptor;
  std::filesystem::path library;
};

// Resolves a plugin path from the configuration ("plugins/physical/atmosphere/isa1976") to a
// library in the plugin directory, opens it, checks the descriptor's core_api_version and
// built_with against the values compiled into libastra_core, and creates the model. Init-time only:
// every failure is a PluginError subclass naming the plugin (errors.hpp). In the
// ASTRA_STATIC_PLUGINS build load() consults the linked-in table instead and this translation unit
// never references dlopen.
//
// Opened libraries stay mapped for the life of the process. A model may outlive the loader, and
// unmapping code under a live vtable is undefined behavior; the process is the unit of a run.
class PluginLoader {
 public:
  explicit PluginLoader(std::filesystem::path plugin_dir);

  [[nodiscard]] LoadedPlugin load(std::string_view plugin_path) const;

  // "plugins/physical/atmosphere/isa1976" -> "libastra_plugins_physical_atmosphere_isa1976.so"
  [[nodiscard]] static std::string library_name(std::string_view plugin_path);

  // Segments of [A-Za-z0-9_] joined by single '/': no leading or trailing '/', no "..", no empty
  // segment.
  [[nodiscard]] static bool is_valid_plugin_path(std::string_view plugin_path) noexcept;

  [[nodiscard]] static constexpr bool is_static() noexcept {
#if defined(ASTRA_STATIC_PLUGINS)
    return true;
#else
    return false;
#endif
  }

  [[nodiscard]] const std::filesystem::path& plugin_dir() const noexcept { return plugin_dir_; }

  // Every plugin path this loader has loaded successfully, once each, in first-load order (a path
  // loaded twice, such as two engines of one turbofan, is one record). Init-time bookkeeping for
  // the run log's /plugins table (ADR-0025); load() stays const for its callers, hence mutable.
  [[nodiscard]] std::span<const PluginLoadRecord> loaded() const noexcept { return loaded_; }

 private:
  void record(std::string_view plugin_path, const PluginDescriptor* descriptor,
              std::filesystem::path library) const;

  std::filesystem::path plugin_dir_;
  mutable std::vector<PluginLoadRecord> loaded_;
};

}  // namespace astra
