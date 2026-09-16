// Inverse-square (Newtonian point-mass) gravity (docs/architecture.md §7, PLAN.md M2 PR 1):
// g_n = [0, 0, g0 * (R/(R+h))^2], purely radial, so only altitude matters -- latitude does not
// enter (that is gravity/wgs84_j2's job). g0 and R both default to the same numbers earth/round
// defaults to, so the pair reproduce a single self-consistent sphere unless a config overrides
// both; they are not linked automatically (a radius concept on EarthModel would mean nothing for
// flat Earth).
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;

// Same defaults as earth/round: standard gravity at the mean Earth radius.
constexpr double kStandardGravity = 9.80665;
constexpr double kDefaultRadius = 6371000.0;

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

class InverseSquareGravity final : public astra::GravityModel {
 public:
  void init(const Config& config, astra::SignalRegistry& registry) override {
    g0_ = checked_finite(config, "g0", config.number("g0", kStandardGravity));
    radius_ = checked_finite(config, "radius", config.number("radius", kDefaultRadius));
    if (radius_ <= 0.0) {
      throw ConfigError(config.origin(), keyed_path(config, "radius"), "radius must be positive");
    }
    registry.add("gravity.g0", &g0_, "m/s^2");
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "gravity"; }

  [[nodiscard]] Eigen::Vector3d g_n(const astra::VehicleState& vs) const noexcept override {
    const double h =
        vs.p_n()(2);  // altitude above the sphere (ADR-0003), NOLINT(readability-magic-numbers)
    const double ratio = radius_ / (radius_ + h);
    return {0.0, 0.0, g0_ * ratio * ratio};
  }

  [[nodiscard]] astra::PositionRepresentation assumed_position_representation()
      const noexcept override {
    return astra::PositionRepresentation::kGeodetic;
  }

  // Codex adversarial review: earth/round's own validate_p_n bounds altitude against *its*
  // configured radius, which says nothing about this plugin's separately configured radius_ --
  // e.g. earth radius 3000 m, gravity radius 2000 m, altitude -2000 m passes earth/round's floor
  // but makes radius_ + h exactly zero in g_n() below. Bound against this plugin's own radius_.
  void validate_p_n(const Eigen::Vector3d& p_n, const Config& node) const override {
    const double alt = p_n(2);  // NOLINT(readability-magic-numbers): p_n's third component
    if (!std::isfinite(alt)) {
      const std::string path = node.path().empty() ? "initial.state.p_n" : node.path() + ".p_n";
      throw ConfigError(node.origin(), path, "p_n's altitude must be finite");
    }
    if (alt <= -radius_) {
      const std::string path = node.path().empty() ? "initial.state.p_n" : node.path() + ".p_n";
      throw ConfigError(node.origin(), path,
                        "p_n's altitude is at or below gravity/inverse_square's own configured "
                        "radius' center -- g_n() divides by (radius + altitude)");
    }
  }

 private:
  double g0_ = kStandardGravity;
  double radius_ = kDefaultRadius;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<InverseSquareGravity>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "gravity_inverse_square",
                                              astra::kCoreApiVersion, astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
