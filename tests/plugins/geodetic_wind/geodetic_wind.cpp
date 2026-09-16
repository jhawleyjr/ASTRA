// A WindModel that declares assumed_position_representation() == kGeodetic and nothing else -- for
// M2 PR 2's position-representation tests (tests/unit/wind/wind_test.cpp): it exists to make
// Simulation::from_config's rejection of a mismatched Earth model assertable, the same class of
// test simulation_physical_config_test.cpp already has for a geodetic GravityModel paired with
// earth/flat. Otherwise trivial: constant zero wind, no state.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/wind_model.hpp"
#include "astra/plugin/descriptor.hpp"

namespace {

class GeodeticWind final : public astra::WindModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "geodetic_wind"; }

  [[nodiscard]] Eigen::Vector3d wind_n(const astra::StepContext& /*ctx*/,
                                       const astra::VehicleState& /*vs*/) const noexcept override {
    return Eigen::Vector3d::Zero();
  }

  [[nodiscard]] astra::PositionRepresentation assumed_position_representation()
      const noexcept override {
    return astra::PositionRepresentation::kGeodetic;
  }
};

std::unique_ptr<astra::Model> create() { return std::make_unique<GeodeticWind>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "geodetic_wind", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
