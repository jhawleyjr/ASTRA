#include "astra/kernel/vehicle.hpp"

#include <utility>

namespace astra {

Vehicle::Vehicle(std::uint32_t index, std::string name) : index_(index), name_(std::move(name)) {}

void Vehicle::init(const PluginLoader& loader, const std::vector<ModelSpec>& models,
                   SignalRegistry& registry) {
  plugins_.reserve(models.size());
  for (const ModelSpec& spec : models) {
    LoadedPlugin loaded = loader.load(spec.plugin_path);
    {
      const SignalRegistry::PrefixScope scope(registry, name_);
      loaded.model->init(spec.config, registry);
    }
    scheduler_.add(*loaded.model, spec.divisor);
    plugins_.push_back(std::move(loaded));
  }
}

}  // namespace astra
