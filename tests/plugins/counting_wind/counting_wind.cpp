// A WindModel that counts and records its own calls, for M2 PR 2's call-contract tests
// (tests/unit/wind/wind_test.cpp): the design in wind_model.hpp claims begin_frame and wind_n are
// each called exactly once per vehicle per frame, in that order, before the wrench stage -- this
// plugin is what makes that assertable instead of only argued. wind_n is const noexcept, per the
// interface, so its call counter is a `mutable` member: an explicit, visible exception for test
// instrumentation only (it observes calls, it never changes what wind_n returns), never the pattern
// a real wind model follows.
#include <algorithm>
#include <limits>
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
    // The min/max p_n north component seen across every begin_frame call THIS FRAME, reset each
    // step() (called once per frame, before any vehicle's begin_frame runs). Two vehicles with
    // distinct p_n give min != max, proving begin_frame ran against each vehicle's OWN
    // VehicleState within one frame, not the same one twice -- a single-vehicle fixture cannot
    // distinguish "once per vehicle per frame" from "once per frame" (Cowork review, post-merge-
    // ready), so this plugin now supports being driven with more than one vehicle
    // (Simulation::for_testing) even though shareable_across_vehicles() -> true already declared
    // it safe to: its own state (call counts, the values below) is correct regardless of how many
    // vehicles share it, unlike a stateful model whose EVOLUTION is keyed to one specific
    // vehicle's airspeed/altitude (dryden, PR 2b).
    registry.add("counting_wind.frame_min_p_n_north", &frame_min_p_n_north_, "m");
    registry.add("counting_wind.frame_max_p_n_north", &frame_max_p_n_north_, "m");
  }
  void reset() override {
    step_calls_ = 0.0;
    begin_frame_calls_ = 0.0;
    wind_n_calls_ = 0.0;
    last_p_n_north_ = 0.0;
    last_altitude_m_ = 0.0;
    frame_min_p_n_north_ = std::numeric_limits<double>::infinity();
    frame_max_p_n_north_ = -std::numeric_limits<double>::infinity();
    latched_wind_.setZero();
  }
  void step(const astra::StepContext& /*ctx*/) override {
    ++step_calls_;
    frame_min_p_n_north_ = std::numeric_limits<double>::infinity();
    frame_max_p_n_north_ = -std::numeric_limits<double>::infinity();
  }
  [[nodiscard]] std::string_view name() const override { return "counting_wind"; }

  void begin_frame(const astra::StepContext& ctx, const astra::VehicleState& vs,
                   double altitude_m) noexcept override {
    ++begin_frame_calls_;
    const double p_n_north = vs.p_n()(0);  // NOLINT(readability-magic-numbers): p_n's 1st component
    last_p_n_north_ = p_n_north;
    last_altitude_m_ = altitude_m;
    frame_min_p_n_north_ = std::min(frame_min_p_n_north_, p_n_north);
    frame_max_p_n_north_ = std::max(frame_max_p_n_north_, p_n_north);
    // Component 0 encodes which frame latched it; component 1 carries the SAME vehicle's own
    // p_n_north above, so wind_n() (called immediately after, for the same vehicle, before any
    // other vehicle's begin_frame runs -- Simulation::step's per-vehicle loop) returns a value
    // that differs per vehicle, not just per frame. A test can therefore check wind_n() encodes
    // exactly this without a separate "did begin_frame already run this frame/for this vehicle"
    // signal, AND prove each vehicle's WrenchSource received its own EnvironmentSample rather than
    // another vehicle's.
    latched_wind_ = {static_cast<double>(ctx.frame), p_n_north, 0.0};
  }

  [[nodiscard]] Eigen::Vector3d wind_n(const astra::StepContext& /*ctx*/,
                                       const astra::VehicleState& /*vs*/) const noexcept override {
    ++wind_n_calls_;  // mutable: test instrumentation only, see file header
    return latched_wind_;
  }

  // Shareable: see the frame_min_p_n_north_/frame_max_p_n_north_ comment above for why this is
  // still accurate with more than one vehicle.
  [[nodiscard]] bool shareable_across_vehicles() const noexcept override { return true; }

 private:
  double step_calls_ = 0.0;
  double begin_frame_calls_ = 0.0;
  mutable double wind_n_calls_ = 0.0;
  double last_p_n_north_ = 0.0;
  double last_altitude_m_ = 0.0;
  double frame_min_p_n_north_ = std::numeric_limits<double>::infinity();
  double frame_max_p_n_north_ = -std::numeric_limits<double>::infinity();
  Eigen::Vector3d latched_wind_ = Eigen::Vector3d::Zero();
};

std::unique_ptr<astra::Model> create() { return std::make_unique<CountingWind>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "counting_wind", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
