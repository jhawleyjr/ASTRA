// A WindModel that counts and records its own calls, for M2 PR 2's call-contract tests
// (tests/unit/wind/wind_test.cpp): the design in wind_model.hpp claims begin_frame and wind_n are
// each called exactly once per vehicle per frame, in that order, before the wrench stage -- this
// plugin is what makes that assertable instead of only argued. wind_n is const noexcept, per the
// interface, so its call counter is a `mutable` member: an explicit, visible exception for test
// instrumentation only (it observes calls, it never changes what wind_n returns), never the pattern
// a real wind model follows.
#include <memory>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/wind_model.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

class CountingWind final : public astra::WindModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& registry) override {
    registry.add("counting_wind.step_calls", &step_calls_, "1");
    registry.add("counting_wind.begin_frame_calls", &begin_frame_calls_, "1");
    registry.add("counting_wind.wind_n_calls", &wind_n_calls_, "1");
    // The p_n north component begin_frame was last handed --
    // WindIsSampledAtFrameStartStateBeforeTheWrenchStage compares this against the vehicle's
    // pre-step state. North (component 0), not altitude (component 2), so this test needs no
    // Earth-model-specific altitude sign convention -- a straight north displacement from a
    // nonzero v_b(0) is unambiguous under every Earth model.
    registry.add("counting_wind.last_p_n_north", &last_p_n_north_, "m");
    // The Earth-derived altitude begin_frame was last handed (wind_model.hpp: never read from
    // vs.p_n() directly -- Codex adversarial review, M2 PR 2). UsesEarthDerivedAltitudeNotRawPN
    // compares this against EarthModel::altitude(vs) computed independently in the test.
    registry.add("counting_wind.last_altitude_m", &last_altitude_m_, "m");
  }
  void reset() override {
    step_calls_ = 0.0;
    begin_frame_calls_ = 0.0;
    wind_n_calls_ = 0.0;
    last_p_n_north_ = 0.0;
    last_altitude_m_ = 0.0;
    latched_wind_.setZero();
  }
  void step(const astra::StepContext& /*ctx*/) override { ++step_calls_; }
  [[nodiscard]] std::string_view name() const override { return "counting_wind"; }

  void begin_frame(const astra::StepContext& ctx, const astra::VehicleState& vs,
                   double altitude_m) noexcept override {
    ++begin_frame_calls_;
    last_p_n_north_ = vs.p_n()(0);  // NOLINT(readability-magic-numbers): p_n's first component
    last_altitude_m_ = altitude_m;
    // Encodes which frame latched it, so a test can check wind_n() returns exactly this without a
    // separate "did begin_frame already run this frame" signal.
    latched_wind_ = {static_cast<double>(ctx.frame), 0.0, 0.0};
  }

  [[nodiscard]] Eigen::Vector3d wind_n(const astra::StepContext& /*ctx*/,
                                       const astra::VehicleState& /*vs*/) const noexcept override {
    ++wind_n_calls_;  // mutable: test instrumentation only, see file header
    return latched_wind_;
  }

  // Shareable: this fixture only ever runs one vehicle, and sharing is not what these tests probe.
  [[nodiscard]] bool shareable_across_vehicles() const noexcept override { return true; }

 private:
  double step_calls_ = 0.0;
  double begin_frame_calls_ = 0.0;
  mutable double wind_n_calls_ = 0.0;
  double last_p_n_north_ = 0.0;
  double last_altitude_m_ = 0.0;
  Eigen::Vector3d latched_wind_ = Eigen::Vector3d::Zero();
};

std::unique_ptr<astra::Model> create() { return std::make_unique<CountingWind>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "counting_wind", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
