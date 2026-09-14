// What a plugin's one exported symbol returns (docs/architecture.md §4, ADR-0009).
#pragma once

#include <cstdint>
#include <memory>

#include "astra/interfaces/model.hpp"

namespace astra {

struct PluginDescriptor {
  const char* category;  // "physical", "hardware", "sim" (or "test" for the test plugins)
  const char* name;
  std::uint32_t core_api_version;  // must equal astra::kCoreApiVersion of the core that loads it
  const char* built_with;          // must equal astra::kBuiltWith of the core that loads it
  std::unique_ptr<Model> (*create)();
};

}  // namespace astra

// Marks the plugin's one exported symbol. Plugins compile with -fvisibility=hidden, so this is the
// only symbol their shared object exports:
//
//   ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
//
// In the ASTRA_STATIC_PLUGINS build the same definition compiles unchanged: the build renames the
// function per plugin and the generated registration table calls it directly
// (cmake/AstraPlugin.cmake).
#define ASTRA_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
