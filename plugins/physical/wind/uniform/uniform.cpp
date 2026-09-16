// Constant, uniform wind (docs/architecture.md §7, PLAN.md M2 PR 2): wind_n is the configured
// (north, east, down) air velocity, independent of vehicle state and time. Publishes the values
// actually used so they land in the log even when a config leaves them at the "still air" default,
// the same reasoning gravity/constant's own header gives for doing the same.
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/wind_model.hpp"
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

class UniformWind final : public astra::WindModel {
 public:
  void init(const Config& config, astra::SignalRegistry& registry) override {
    wind_n_(0) = checked_finite(config, "north", config.number("north", 0.0));
    wind_n_(1) = checked_finite(config, "east", config.number("east", 0.0));
    wind_n_(2) = checked_finite(config, "down", config.number("down", 0.0));
    registry.add("wind.north", &wind_n_(0), "m/s");
    registry.add("wind.east", &wind_n_(1), "m/s");
    registry.add("wind.down", &wind_n_(2), "m/s");
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "wind"; }

  // Constant: begin_frame has nothing to evolve, so this plugin does not override it
  // (wind_model.hpp's default is already the empty no-op this would be).

  [[nodiscard]] Eigen::Vector3d wind_n(const astra::StepContext& /*ctx*/,
                                       const astra::VehicleState& /*vs*/) const noexcept override {
    return wind_n_;
  }

  // Stateless and reads no VehicleState: sharing it across more than one vehicle is exact, not
  // approximate (wind_model.hpp).
  [[nodiscard]] bool shareable_across_vehicles() const noexcept override { return true; }

 private:
  Eigen::Vector3d wind_n_ = Eigen::Vector3d::Zero();
};

std::unique_ptr<astra::Model> create() { return std::make_unique<UniformWind>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "wind_uniform", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
