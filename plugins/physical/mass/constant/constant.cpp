// Constant mass properties (docs/architecture.md §7; plugins/physical/mass/constant per PLAN.md M1
// PR 3): mass and a full symmetric inertia tensor about the CG, read from six named scalars because
// Config has no matrix getter. Off-diagonal terms default to zero. The tensor built this way is
// symmetric by construction -- there is no way to submit an asymmetric one through this schema --
// so init validates only what can actually go wrong: a non-finite or non-positive value, and a
// tensor that is finite and symmetric but not positive-definite (§15's pre-run check). Each failure
// is a ConfigError naming the offending key, so a bad config never reaches the EOM.
//
// Config keys ixx/iyy/izz are the moments of inertia; ixy/ixz/iyz are PRODUCTS OF INERTIA in the
// S&L convention (Ixy = integral(x*y*dm), Ixz = integral(x*z*dm), Iyz = integral(y*z*dm), body FRD
// axes about the CG, kg*m^2 -- Stevens & Lewis, "Aircraft Control and Simulation"; M7 PR 4). A
// published product of inertia is entered as printed; a source that tabulates the inertia tensor's
// own off-diagonal entries needs its sign flipped. The TENSOR used in the equations of motion
// (H = I*omega) carries their negations (tensor_xz = -integral(x*z*dm)), formed below.
// aircraft/_template's and aircraft/notional_transport's mass plugins read the same keys in the
// same convention.
//
// The negation is written 0.0 - p, not -p (M7 PR 4, D1). The two have the same bits for every
// nonzero p, but an omitted key (0.0) must give the tensor the bits it had before M7 PR 4, +0.0 off
// the diagonal, where -p would give -0.0. That is what keeps M2's NESC cases 1 to 10 and the R1
// golden (a bitwise comparison of trajectory states) unchanged by construction: no tracked config
// sets a product key except one this plugin refuses. ASTRA builds without -ffast-math, so the
// compiler cannot fold 0.0 - p into -p. aircraft/_template's and aircraft/notional_transport's mass
// plugins keep -p on purpose: their -0.0 bits are already frozen into M3's cruise regression, the
// R1 end-to-end fixture's golden and M7 PR 3b's template-identity evidence, and M7 PR 4 left their
// expressions alone.
//
// Config keys:
//   mass | number | kg     | required | The vehicle's mass, finite and positive.
//   ixx  | number | kg*m^2 | required | Moment of inertia about body x (FRD) through the CG. The
//          tensor built from all six keys must be positive-definite.
//   iyy  | number | kg*m^2 | required | Moment of inertia about body y (FRD) through the CG.
//   izz  | number | kg*m^2 | required | Moment of inertia about body z (FRD) through the CG.
//   ixy  | number | kg*m^2 | 0        | S&L product of inertia, Ixy = integral(x*y*dm), body FRD
//          axes about the CG, entered as published; negated into the tensor's xy entry.
//   ixz  | number | kg*m^2 | 0        | S&L product of inertia, Ixz = integral(x*z*dm), body FRD
//          axes about the CG, entered as published; negated into the tensor's xz entry.
//   iyz  | number | kg*m^2 | 0        | S&L product of inertia, Iyz = integral(y*z*dm), body FRD
//          axes about the CG, entered as published; negated into the tensor's yz entry.
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

    // Products of inertia (S&L convention) negated into the tensor as 0.0 - p, not -p: an omitted
    // key keeps the pre-M7-PR-4 +0.0 (see the file header).
    const double tensor_xy = 0.0 - ixy;
    const double tensor_xz = 0.0 - ixz;
    const double tensor_yz = 0.0 - iyz;

    Eigen::Matrix3d inertia_b;
    // clang-format off
    inertia_b << ixx,       tensor_xy, tensor_xz,
                 tensor_xy, iyy,       tensor_yz,
                 tensor_xz, tensor_yz, izz;
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
