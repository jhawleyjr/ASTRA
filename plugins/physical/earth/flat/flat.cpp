// The flat Earth model (docs/architecture.md §6, ADR-0003): p_n is NED metres from the origin, the
// Earth rotation rate and the transport rate of n are identically zero, and altitude is the negated
// down component of p_n. Reads no config. Every later Earth model (round, WGS-84 rotating, M2)
// gives these same three quantities their real values with no change to core/kernel/eom.cpp.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/util/frames.hpp"

namespace {

class FlatEarth final : public astra::EarthModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "earth"; }

  [[nodiscard]] Eigen::Vector3d p_n_dot(const astra::VehicleState& vs) const noexcept override {
    return astra::frames::rotate_b_to_n(vs.q_nb(), vs.v_b());
  }
  [[nodiscard]] Eigen::Vector3d omega_ie_n(
      const astra::VehicleState& /*vs*/) const noexcept override {
    return Eigen::Vector3d::Zero();
  }
  [[nodiscard]] Eigen::Vector3d omega_en_n(
      const astra::VehicleState& /*vs*/) const noexcept override {
    return Eigen::Vector3d::Zero();
  }
  [[nodiscard]] double altitude(const astra::VehicleState& vs) const noexcept override {
    constexpr int kDown = astra::state::kPositionSize - 1;  // NED: third component is down
    return -vs.p_n()(kDown);
  }
};

std::unique_ptr<astra::Model> create() { return std::make_unique<FlatEarth>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "earth_flat", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
