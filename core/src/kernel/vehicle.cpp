#include "astra/kernel/vehicle.hpp"

#include <utility>

#include "astra/errors.hpp"
#include "astra/interfaces/mass_properties_model.hpp"

namespace astra {

Vehicle::Vehicle(std::uint32_t index, std::string name) : index_(index), name_(std::move(name)) {}

void Vehicle::init(const PluginLoader& loader, const std::vector<ModelSpec>& models,
                   SignalRegistry& registry, const PhysicsSpec* physics) {
  plugins_.reserve(models.size() + (physics != nullptr ? 1 : 0));
  for (const ModelSpec& spec : models) {
    LoadedPlugin loaded = loader.load(spec.plugin_path);
    {
      const SignalRegistry::PrefixScope scope(registry, name_);
      loaded.model->init(spec.config, registry);
    }
    scheduler_.add(*loaded.model, spec.divisor);
    plugins_.push_back(std::move(loaded));
  }

  if (physics != nullptr) {
    LoadedPlugin mass_loaded = loader.load(physics->mass.plugin_path);
    auto* mass_model = dynamic_cast<MassPropertiesModel*>(mass_loaded.model.get());
    if (mass_model == nullptr) {
      throw PluginLoadError(physics->mass.plugin_path, "does not implement MassPropertiesModel");
    }
    {
      const SignalRegistry::PrefixScope scope(registry, name_);
      mass_model->init(physics->mass.config, registry);
    }
    scheduler_.add(*mass_model, physics->mass.divisor);
    eom_.emplace(*physics->earth, *physics->gravity, *mass_model);
    plugins_.push_back(std::move(mass_loaded));
  }
}

}  // namespace astra
