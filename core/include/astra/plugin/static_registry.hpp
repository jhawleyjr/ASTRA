// The ASTRA_STATIC_PLUGINS registration table (ADR-0009): every plugin's entry point, linked into
// libastra_core and called directly, so a real-time build never touches dlopen.
#pragma once

#if defined(ASTRA_STATIC_PLUGINS)

#include <span>

#include "astra/plugin/descriptor.hpp"

namespace astra {

struct StaticPluginEntry {
  const char* path;  // the plugin path, e.g. "plugins/physical/atmosphere/isa1976"
  const PluginDescriptor* (*entry)();  // that plugin's astra_plugin(), renamed by the build
};

// Defined by the file astra_finalize_plugins() generates into astra_core (cmake/AstraPlugin.cmake).
[[nodiscard]] std::span<const StaticPluginEntry> static_plugin_table() noexcept;

}  // namespace astra

#endif  // ASTRA_STATIC_PLUGINS
