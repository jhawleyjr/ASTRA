#include "astra/kernel/vehicle.hpp"

#include <utility>

#include "astra/errors.hpp"
#include "astra/interfaces/mass_properties_model.hpp"

namespace astra {

Vehicle::Vehicle(std::uint32_t index, std::string name) : index_(index), name_(std::move(name)) {}

void Vehicle::init(const PluginLoader& loader, const std::vector<ModelSpec>& models,
                   SignalRegistry& registry, const PhysicsSpec* physics) {
  state_names_ = {name_ + ".state.p_n", name_ + ".state.v_b", name_ + ".state.q_nb",
                  name_ + ".state.omega_b"};

  plugins_.reserve(models.size() + (physics != nullptr ? 1 : 0));
  wrench_sources_.reserve(models.size());  // upper bound: not every model is a WrenchSource
  for (const ModelSpec& spec : models) {
    LoadedPlugin loaded = loader.load(spec.plugin_path);
    {
      const SignalRegistry::PrefixScope scope(registry, name_);
      loaded.model->init(spec.config, registry);
    }
    scheduler_.add(*loaded.model, spec.divisor);
    // Captured before the move below: .get() does not affect ownership, and the model stays alive
    // for the vehicle's lifetime once plugins_ owns it.
    if (auto* const wrench_source = dynamic_cast<WrenchSource*>(loaded.model.get());
        wrench_source != nullptr) {
      wrench_sources_.push_back(WrenchEntry{wrench_source, spec.plugin_path});
    }
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

HaltReason Vehicle::integrate(const Integrator& integrator, const StepContext& ctx,
                              HealthMonitor& health) noexcept {
  if (!eom_.has_value()) {
    return {};
  }

  Wrench total;
  const VehicleState vs{state_};
  for (const WrenchEntry& entry : wrench_sources_) {
    const Wrench contribution = entry.model->wrench_b(vs);
    if (const HaltReason halt = health.check_wrench(contribution, entry.plugin_path, ctx, index_);
        halt) {
      return halt;  // state_ untouched: the EOM never sees this contribution.
    }
    total.force_b += contribution.force_b;
    total.moment_b += contribution.moment_b;
  }

  eom_->set_applied_wrench(total);
  eom_->begin_frame();
  integrator.step(*eom_, ctx.t, state_);

  const StateNames names{state_names_[0], state_names_[1], state_names_[2], state_names_[3]};
  const HaltReason state_halt = health.check_state(state_, names, ctx, index_);
  // Unconditional: ADR-0001 pins renormalization after every integrator step, halt or not. A
  // non-finite quaternion leaves post_step_renormalize's own false return, and q untouched -- there
  // is nothing to normalize to, and the halt above already names it.
  static_cast<void>(post_step_renormalize(state_));
  return state_halt;
}

}  // namespace astra
