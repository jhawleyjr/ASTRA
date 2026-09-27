#include "astra/plugin/loader.hpp"

#include <cstring>
#include <string>
#include <utility>

#include "astra/core_api.hpp"
#include "astra/errors.hpp"

#if defined(ASTRA_STATIC_PLUGINS)
#include "astra/plugin/static_registry.hpp"
#else
#include <dlfcn.h>
#endif

namespace astra {

namespace {

bool is_path_char(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Checks the descriptor against this core and creates the model; the init-time contract of
// ADR-0009.
LoadedPlugin check_and_create(std::string_view plugin_path, const PluginDescriptor* descriptor) {
  if (descriptor == nullptr) {
    throw PluginLoadError(plugin_path, "astra_plugin() returned a null descriptor");
  }
  if (descriptor->core_api_version != core_api_version()) {
    throw PluginApiVersionError(plugin_path, core_api_version(), descriptor->core_api_version);
  }
  const char* actual_toolchain =
      descriptor->built_with == nullptr ? "(null)" : descriptor->built_with;
  if (std::strcmp(actual_toolchain, built_with()) != 0) {
    throw PluginToolchainError(plugin_path, built_with(), actual_toolchain);
  }
  if (descriptor->create == nullptr) {
    throw PluginLoadError(plugin_path, "descriptor has no create()");
  }
  std::unique_ptr<Model> model = descriptor->create();
  if (model == nullptr) {
    throw PluginLoadError(plugin_path, "create() returned a null model");
  }
  return LoadedPlugin{descriptor, std::move(model)};
}

#if defined(ASTRA_STATIC_PLUGINS)

const PluginDescriptor* resolve_entry(std::string_view plugin_path) {
  for (const StaticPluginEntry& entry : static_plugin_table()) {
    if (plugin_path == entry.path) {
      return entry.entry();
    }
  }
  throw PluginNotFoundError(plugin_path,
                            "not in the static plugin table of this ASTRA_STATIC_PLUGINS build");
}

#else

std::string dlerror_text() {
  const char* text = ::dlerror();
  return text == nullptr ? std::string{"unknown dlopen error"} : std::string{text};
}

const PluginDescriptor* resolve_entry(std::string_view plugin_path,
                                      const std::filesystem::path& library) {
  if (!std::filesystem::exists(library)) {
    throw PluginNotFoundError(plugin_path,
                              "no library " + library.string() + " (is the plugin built?)");
  }
  // Never dlclosed: see the class comment in loader.hpp.
  void* handle = ::dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    throw PluginLoadError(plugin_path, "dlopen failed: " + dlerror_text());
  }
  void* symbol = ::dlsym(handle, "astra_plugin");
  if (symbol == nullptr) {
    throw PluginLoadError(plugin_path, library.string() + " exports no astra_plugin symbol");
  }
  using EntryPoint = const PluginDescriptor* (*)();
  return reinterpret_cast<EntryPoint>(symbol)();
}

#endif

}  // namespace

PluginLoader::PluginLoader(std::filesystem::path plugin_dir) : plugin_dir_(std::move(plugin_dir)) {}

bool PluginLoader::is_valid_plugin_path(std::string_view plugin_path) noexcept {
  if (plugin_path.empty() || plugin_path.front() == '/' || plugin_path.back() == '/') {
    return false;
  }
  bool at_segment_start = true;
  for (const char c : plugin_path) {
    if (c == '/') {
      if (at_segment_start) {
        return false;
      }
      at_segment_start = true;
      continue;
    }
    if (!is_path_char(c)) {
      return false;
    }
    at_segment_start = false;
  }
  return true;
}

std::string PluginLoader::library_name(std::string_view plugin_path) {
  std::string name = "libastra_";
  for (const char c : plugin_path) {
    name.push_back(c == '/' ? '_' : c);
  }
  name += ".so";
  return name;
}

LoadedPlugin PluginLoader::load(std::string_view plugin_path) const {
  if (!is_valid_plugin_path(plugin_path)) {
    throw PluginNotFoundError(
        plugin_path, "invalid plugin path: expected segments of [A-Za-z0-9_] separated by '/'");
  }
#if defined(ASTRA_STATIC_PLUGINS)
  LoadedPlugin loaded = check_and_create(plugin_path, resolve_entry(plugin_path));
  record(plugin_path, loaded.descriptor, {});
#else
  std::filesystem::path library = plugin_dir_ / library_name(plugin_path);
  LoadedPlugin loaded = check_and_create(plugin_path, resolve_entry(plugin_path, library));
  record(plugin_path, loaded.descriptor, std::move(library));
#endif
  return loaded;
}

void PluginLoader::record(std::string_view plugin_path, const PluginDescriptor* descriptor,
                          std::filesystem::path library) const {
  for (const PluginLoadRecord& existing : loaded_) {
    if (existing.plugin_path == plugin_path) {
      return;
    }
  }
  loaded_.push_back(PluginLoadRecord{std::string{plugin_path}, descriptor, std::move(library)});
}

}  // namespace astra
