// A test-only WrenchSource that records the Earth rate (ADR-0032, M7 PR 3b) and the altitude
// (ADR-0033, M7 PR 3c) each RK4 stage hands it. Its users:
// - tests/unit/kernel/simulation_earth_rate_test.cpp;
// - tests/unit/kernel/vehicle_environment_test.cpp;
// - tests/physics/no_alloc_test.cpp.
//
// What it is: it requires no environment and owns one state with a zero derivative, so every
// stage calls it from both wrench_b() and integrate_state().
//
// It is also env.altitude_m's reader: wrench_b() returns force_b.x() = altitude_m - altitude_m,
// and zero in every other component.
// - For every finite altitude that is +0.0 (x - x under IEEE round-to-nearest), so inside a
//   simulation, where the core always sets the field, the wrench is zero bit for bit, exactly as
//   it was before M7 PR 3c.
// - For an unset altitude (EnvironmentSample's NaN default) it is NaN: a sample built outside the
//   stage path that forgets the field gives a NaN wrench, never a number.
//
// What it records: slot k of each kind is the k-th call since the last step() (or since init, for
// a probe). There are four slots, and a later call only counts. Each slot records:
// - env.omega_ie_b;
// - air_data::omega_eb_b(vs, env);
// - env.altitude_m;
// - the stage's q_nb and p_n.
// step() resets every slot to NaN and both counts to zero, so a stage that never called is
// visible.
//
// Published signals, under the vehicle prefix:
// - "earth_rate_recorder.<kind>.<k>.<field>_<i>", with kind wrench or state, and field omega_ie_b
//   (i 0 to 2), omega_eb_b (0 to 2), q_nb (0 to 3) or p_n (0 to 2);
// - "earth_rate_recorder.<kind>.<k>.altitude_m";
// - "earth_rate_recorder.<kind>_calls".
//
// The mutable members are test instrumentation only, the same exception
// tests/plugins/atmosphere_consumer documents for its own const wrench_b(). Per-frame path: the
// recording is plain array writes, with no allocation.
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/air_data.hpp"

namespace {

constexpr std::size_t kSlots = 4;
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

struct Record {
  std::array<double, 3> omega_ie_b{kNan, kNan, kNan};
  std::array<double, 3> omega_eb_b{kNan, kNan, kNan};
  std::array<double, 4> q_nb{kNan, kNan, kNan, kNan};
  std::array<double, 3> p_n{kNan, kNan, kNan};
  double altitude_m = kNan;
};

struct Recording {
  std::array<Record, kSlots> slots{};
  double calls = 0.0;

  void reset() noexcept {
    slots.fill(Record{});
    calls = 0.0;
  }

  void record(const astra::VehicleState& vs, const astra::EnvironmentSample& env) noexcept {
    const auto index = static_cast<std::size_t>(calls);
    calls += 1.0;
    if (index >= kSlots) {
      return;
    }
    Record& slot = slots.at(index);
    const Eigen::Vector3d omega_eb_b = astra::air_data::omega_eb_b(vs, env);
    const Eigen::Vector4d q_nb = vs.q_nb();
    const Eigen::Vector3d p_n = vs.p_n();
    for (Eigen::Index i = 0; i < 3; ++i) {
      slot.omega_ie_b.at(static_cast<std::size_t>(i)) = env.omega_ie_b(i);
      slot.omega_eb_b.at(static_cast<std::size_t>(i)) = omega_eb_b(i);
      slot.p_n.at(static_cast<std::size_t>(i)) = p_n(i);
    }
    for (Eigen::Index i = 0; i < 4; ++i) {
      slot.q_nb.at(static_cast<std::size_t>(i)) = q_nb(i);
    }
    slot.altitude_m = env.altitude_m;
  }

  void publish(astra::SignalRegistry& registry, const std::string& prefix) {
    registry.add(prefix + "_calls", &calls, "");
    for (std::size_t k = 0; k < kSlots; ++k) {
      Record& slot = slots.at(k);
      const std::string base = prefix + "." + std::to_string(k) + ".";
      for (std::size_t i = 0; i < 3; ++i) {
        registry.add(base + "omega_ie_b_" + std::to_string(i), &slot.omega_ie_b.at(i), "rad/s");
        registry.add(base + "omega_eb_b_" + std::to_string(i), &slot.omega_eb_b.at(i), "rad/s");
        registry.add(base + "p_n_" + std::to_string(i), &slot.p_n.at(i), "");
      }
      for (std::size_t i = 0; i < 4; ++i) {
        registry.add(base + "q_nb_" + std::to_string(i), &slot.q_nb.at(i), "");
      }
      registry.add(base + "altitude_m", &slot.altitude_m, "m");
    }
  }
};

class EarthRateRecorder final : public astra::WrenchSource {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& registry) override {
    wrench_.publish(registry, "earth_rate_recorder.wrench");
    state_.publish(registry, "earth_rate_recorder.state");
  }
  void reset() override {
    wrench_.reset();
    state_.reset();
  }
  void step(const astra::StepContext& /*ctx*/) override {
    wrench_.reset();
    state_.reset();
  }
  [[nodiscard]] std::string_view name() const override { return "earth_rate_recorder"; }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& vs, const astra::EnvironmentSample& env) const noexcept override {
    wrench_.record(vs, env);
    astra::Wrench wrench;
    wrench.force_b.x() = env.altitude_m - env.altitude_m;  // +0.0 when set, NaN when not
    return wrench;
  }

  [[nodiscard]] int state_count() const noexcept override { return 1; }
  void initial_state(double* q) const noexcept override { q[0] = 0.0; }
  void integrate_state(double /*t*/, const astra::VehicleState& vs,
                       const astra::EnvironmentSample& env, double* dq) const noexcept override {
    state_.record(vs, env);
    dq[0] = 0.0;
  }

 private:
  mutable Recording wrench_;
  mutable Recording state_;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<EarthRateRecorder>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "earth_rate_recorder", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
