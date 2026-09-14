// Loads plugins by path at init (docs/architecture.md §4, ADR-0009).
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "astra/interfaces/model.hpp"
#include "astra/plugin/descriptor.hpp"

namespace astra {

struct LoadedPlugin {
  const PluginDescriptor* descriptor;
  std::unique_ptr<Model> model;
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

 private:
  std::filesystem::path plugin_dir_;
};

}  // namespace astra
