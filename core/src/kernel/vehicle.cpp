#include "astra/kernel/vehicle.hpp"

#include <utility>

#include "astra/errors.hpp"
#include "astra/interfaces/mass_properties_model.hpp"

namespace astra {

Vehicle::Vehicle(std::uint32_t index, std::string name) : index_(index), name_(std::move(name)) {}

void Vehicle::init(const PluginLoader& loader, const std::vector<ModelSpec>& models,
                   SignalRegistry& registry, const PhysicsSpec* physics,
                   EnvironmentAvailability availability) {
  state_names_ = {name_ + ".state.p_n", name_ + ".state.v_b", name_ + ".state.q_nb",
                  name_ + ".state.omega_b"};
  wrench_total_name_ = name_ + ".wrench_total";

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
      const EnvironmentRequirements requirements = wrench_source->requires_environment();
      if (requirements.wind && !availability.wind) {
        throw ConfigError(spec.config.origin(), "physical.wind",
                          spec.plugin_path + " requires wind");
      }
      if (requirements.atmosphere) {
        // Unconditional, not "if physical.atmosphere is absent": no PR before M2 PR 3 populates
        // EnvironmentSample::atmosphere (environment_sample.hpp), so even a config naming
        // physical.atmosphere cannot satisfy this yet -- nothing connects the two before PR 3.
        throw ConfigError(spec.config.origin(), "physical.atmosphere",
                          spec.plugin_path + " requires atmosphere");
      }
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
    const Wrench contribution = entry.model->wrench_b(vs, environment_);
    if (const HaltReason halt = health.check_wrench(contribution, entry.plugin_path, ctx, index_);
        halt) {
      return halt;  // state_ untouched: the EOM never sees this contribution.
    }
    total.force_b += contribution.force_b;
    total.moment_b += contribution.moment_b;
  }

  // Codex review: individually finite contributions can still sum to a non-finite total (e.g. two
  // sources each near DBL_MAX), which the per-contribution checks above cannot catch -- check the
  // accumulated wrench too, before it reaches the EOM.
  if (const HaltReason halt = health.check_wrench(total, wrench_total_name_, ctx, index_); halt) {
    return halt;  // state_ untouched, same as a halting per-contribution check above.
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
