// Constant mass properties starting point for a new aircraft (docs/architecture.md §7, §18;
// PLAN.md M3 PR 1). `just new-aircraft <name>` (or `cp -r aircraft/_template aircraft/<name>`)
// copies this file as-is: ADR-0009/cmake/AstraPlugin.cmake derive the plugin's identity from its
// own directory, so nothing here needs editing except the numbers in the aircraft's own config --
// see aircraft/_template/configs/cruise.yaml's TODO-marked mass block. Left un-edited, this
// plugin's own "mass must be positive" check below throws a ConfigError naming the config key that
// still reads 0.0, so an un-edited template can never silently run.
//
// Config keys ixy/ixz/iyz are PRODUCTS OF INERTIA in the S&L / published-data-table convention
// (Ixz = integral(x*z*dm), positive-definition -- Stevens & Lewis, "Aircraft Control and
// Simulation," and most published aircraft mass-property sources use this convention). The inertia
// TENSOR used in the equations of motion (H = I*omega) carries the off-diagonal terms negated
// (tensor_xz = -integral(x*z*dm)); this plugin does that negation below, so a number transcribed
// from a source that documents itself in the product-of-inertia convention can be entered directly
// without the reader flipping its sign by hand.
//
// plugins/physical/mass/constant reads its ixy/ixz/iyz in this same convention (M7 PR 4).
//
// Everything below stays in an anonymous namespace: the ASTRA_STATIC_PLUGINS build (one of
// `just check`'s four presets) links every plugin -- including every aircraft's own copy of this
// exact file -- into one binary, and cmake/AstraPlugin.cmake only renames the single exported
// astra_plugin symbol per plugin. Any other external-linkage name here would collide with another
// aircraft's copy at link time. Keep this discipline when editing a copy for a real aircraft.
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

class TemplateMass final : public astra::MassPropertiesModel {
 public:
  void init(const Config& config, astra::SignalRegistry& registry) override {
    const double mass = checked_finite(config, "mass", config.number("mass"));
    if (mass <= 0.0) {
      throw ConfigError(config.origin(), keyed_path(config, "mass"), "mass must be positive");
    }
    const double ixx = checked_finite(config, "ixx", config.number("ixx"));
    const double iyy = checked_finite(config, "iyy", config.number("iyy"));
    const double izz = checked_finite(config, "izz", config.number("izz"));
    // Products of inertia (S&L convention -- see file header); default zero.
    const double ixy = checked_finite(config, "ixy", config.number("ixy", 0.0));
    const double ixz = checked_finite(config, "ixz", config.number("ixz", 0.0));
    const double iyz = checked_finite(config, "iyz", config.number("iyz", 0.0));

    Eigen::Matrix3d inertia_b;
    // clang-format off
    inertia_b << ixx, -ixy, -ixz,
                -ixy,  iyy, -iyz,
                -ixz, -iyz,  izz;
    // clang-format on

    // Symmetric by construction; only positive-definiteness can still fail (§15: "inertia tensor
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

std::unique_ptr<astra::Model> create() { return std::make_unique<TemplateMass>(); }

// "mass", not "template_mass": new-aircraft.sh only rewrites config YAML paths on copy, never
// .cpp string literals, so a per-aircraft-specific name here would stay wrong (still saying
// "template") in every aircraft ever copied from this file. The plugin path itself already
// disambiguates which aircraft's copy a run log's descriptor came from.
constexpr astra::PluginDescriptor kDescriptor{"physical", "mass", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
