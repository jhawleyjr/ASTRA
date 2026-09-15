// Constant mass properties (docs/architecture.md §7; plugins/physical/mass/constant per PLAN.md M1
// PR 3): mass and a full symmetric inertia tensor about the CG, read from six named scalars because
// Config has no matrix getter. Off-diagonal terms default to zero. The tensor built this way is
// symmetric by construction -- there is no way to submit an asymmetric one through this schema --
// so init validates only what can actually go wrong: a non-finite or non-positive value, and a
// tensor that is finite and symmetric but not positive-definite (§15's pre-run check). Each failure
// is a ConfigError naming the offending key, so a bad config never reaches the EOM.
#include <Eigen/Cholesky>
#include <Eigen/Core>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/mass_properties_model.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;

std::string keyed_path(const Config& config, std::string_view key) {
  return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
}

double checked_finite(const Config& config, std::string_view key, double value) {
  if (!std::isfinite(value)) {
    throw ConfigError(config.origin(), keyed_path(config, key),
                      std::string{key} + " must be finite");
  }
  return value;
}

class ConstantMass final : public astra::MassPropertiesModel {
 public:
  void init(const Config& config, astra::SignalRegistry& registry) override {
    const double mass = checked_finite(config, "mass", config.number("mass"));
    if (mass <= 0.0) {
      throw ConfigError(config.origin(), keyed_path(config, "mass"), "mass must be positive");
    }
    const double ixx = checked_finite(config, "ixx", config.number("ixx"));
    const double iyy = checked_finite(config, "iyy", config.number("iyy"));
    const double izz = checked_finite(config, "izz", config.number("izz"));
    const double ixy = checked_finite(config, "ixy", config.number("ixy", 0.0));
    const double ixz = checked_finite(config, "ixz", config.number("ixz", 0.0));
    const double iyz = checked_finite(config, "iyz", config.number("iyz", 0.0));

    Eigen::Matrix3d inertia_b;
    // clang-format off
    inertia_b << ixx, ixy, ixz,
                 ixy, iyy, iyz,
                 ixz, iyz, izz;
    // clang-format on

    // Built symmetric above; only positive-definiteness can still fail (§15: "inertia tensor
    // symmetric positive-definite").
    const Eigen::LLT<Eigen::Matrix3d> llt(inertia_b);
    if (llt.info() != Eigen::Success) {
      throw ConfigError(config.origin(), config.path(),
                        "inertia tensor (ixx, iyy, izz, ixy, ixz, iyz) must be positive-definite");
    }

    props_.mass = mass;
    props_.inertia_b = inertia_b;
    registry.add("mass.mass", &props_.mass, "kg");
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "mass"; }

  [[nodiscard]] astra::MassProps get() const noexcept override { return props_; }

 private:
  astra::MassProps props_;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<ConstantMass>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "mass_constant", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
