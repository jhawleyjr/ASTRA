// Q5's aero-only channel route (M7 PR 7, docs/plans/m7-pr7-aero-evaluator.md; the M7 plan's Q5
// under (c), with its 2026-10-08 amendment and amendment 6). NESC's aero_body* columns are aero
// only, and the sim publishes only v0.wrench_total.* (aero plus propulsion), so the harness
// evaluates its own second instance of a WrenchSource plugin at each sample's frame-start (k1)
// state and environment. This is M2 PR 3c's R12 pattern (nesc_cases_test.cpp), generalized in two
// ways.
//
// FrameStartEnvironment rebuilds the k1 stage's EnvironmentSample outside the stage path:
// - It starts from the vehicle's latched sample, Vehicle::environment(), which after run(1) is
// still
//   that frame's. Its wind_n is the one every stage copies (Vehicle::evaluate_derivative_stage).
//   Its atmosphere is the k1 stage's: Simulation::sample_environment and
//   Vehicle::prepare_stage_environment sample it at the same frame-start state, through the same
//   Earth model's altitude.
// - The latched sample keeps omega_ie_b and altitude_m NaN (environment_sample.hpp), so this sets
//   both at the state, as the stage does: omega_ie_b through the core's own earth_rate_b
//   (kernel/vehicle.hpp; ADR-0032, its +0.0 included) and altitude_m through EarthModel::altitude
//   (ADR-0033).
// - "The sim's Earth model" is a second instance of the config's own physical.earth plugin, loaded
//   from the same node. Simulation exposes none, and every shipped Earth plugin is stateless: its
//   step() is a no-op and its init reads only its own node.
//
// IndependentWrenchSource is a second instance of one vehicle.models plugin, with its own
// SignalRegistry and InputRegistry:
// - copy_inputs() writes the sim's latched "v0." + name for each of the instance's own input names
//   (the F-16's controls.elevator, controls.aileron, controls.rudder and controls.throttle), then
//   latches. at() calls wrench_b(). They are two calls, so a negative control can evaluate with the
//   previous sample's inputs.
// - It is valid only for a plugin that binds no own or peer continuous state (Q5's condition,
//   critique C-07; the F-16's aero and engine, docs/plans/m7-f16-inventory.md section 10). An
//   instance with a nonzero state_count() is refused. A peer binding (resolve_peers) cannot be seen
//   from outside, and the self-check (f16_evaluator_test.cpp) is its guard.
// - The rate the F-16 aero damps comes from the sample's state and this environment, never from a
//   peer binding (Q5's sub-condition).
//
// matches_relative() is the self-check's criterion (the plan's D5).
//
// Test-only, init-time code: it allocates and throws astra::Error, naming what is missing.
#pragma once

#include <gtest/gtest.h>

#include <Eigen/Core>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/vehicle.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"

namespace astra::test::nesc {

// A plugin slot's path in either of docs/architecture.md §13's spellings: a plain scalar, or a map
// with its own config next to "plugin" (simulation.cpp's resolve_model_spec).
inline std::string plugin_path_of(const Config& node) {
  return node.is_scalar() ? node.text() : node.text("plugin");
}

class FrameStartEnvironment {
 public:
  // config is the document the Simulation was built from.
  FrameStartEnvironment(const Config& config, const PluginLoader& loader)
      : earth_node_(config.at("physical.earth")),
        plugin_path_(plugin_path_of(earth_node_)),
        loaded_(loader.load(plugin_path_)) {
    earth_ = dynamic_cast<EarthModel*>(loaded_.model.get());
    if (earth_ == nullptr) {
      throw Error("FrameStartEnvironment: " + plugin_path_ + " does not implement EarthModel");
    }
    earth_->init(earth_node_, registry_);
  }
  FrameStartEnvironment(const FrameStartEnvironment&) = delete;
  FrameStartEnvironment& operator=(const FrameStartEnvironment&) = delete;
  FrameStartEnvironment(FrameStartEnvironment&&) = delete;
  FrameStartEnvironment& operator=(FrameStartEnvironment&&) = delete;
  ~FrameStartEnvironment() = default;

  // latched is the vehicle's Vehicle::environment() for the frame whose pre-step state is vs.
  [[nodiscard]] EnvironmentSample at(const EnvironmentSample& latched,
                                     const VehicleState& vs) const noexcept {
    EnvironmentSample env = latched;
    env.omega_ie_b = earth_rate_b(*earth_, vs);
    env.altitude_m = earth_->altitude(vs);
    return env;
  }

 private:
  Config earth_node_;
  std::string plugin_path_;
  SignalRegistry registry_;
  LoadedPlugin loaded_;
  EarthModel* earth_ = nullptr;
};

class IndependentWrenchSource {
 public:
  // model_node is the vehicle.models entry the sim loaded the plugin from; sim supplies the latched
  // inputs, resolved here once.
  IndependentWrenchSource(const Config& model_node, const PluginLoader& loader,
                          const Simulation& sim)
      : plugin_path_(plugin_path_of(model_node)), loaded_(loader.load(plugin_path_)) {
    source_ = dynamic_cast<WrenchSource*>(loaded_.model.get());
    if (source_ == nullptr) {
      throw Error("IndependentWrenchSource: " + plugin_path_ + " does not implement WrenchSource");
    }
    loaded_.model->init(model_node, registry_);
    if (loaded_.model->state_count() != 0) {
      throw Error("IndependentWrenchSource: " + plugin_path_ +
                  " owns continuous state, which a second instance cannot reproduce (Q5, C-07)");
    }
    source_->bind_inputs(inputs_, registry_);
    for (const std::string_view name : inputs_.names()) {
      const std::string sim_name = "v0." + std::string{name};
      const double* latched = sim.registry().find(sim_name);
      if (latched == nullptr) {
        throw Error("IndependentWrenchSource: " + plugin_path_ + " reads " + std::string{name} +
                    ", but the sim has no " + sim_name);
      }
      sim_inputs_.push_back(SimInput{.name = std::string{name}, .latched = latched});
    }
  }
  IndependentWrenchSource(const IndependentWrenchSource&) = delete;
  IndependentWrenchSource& operator=(const IndependentWrenchSource&) = delete;
  IndependentWrenchSource(IndependentWrenchSource&&) = delete;
  IndependentWrenchSource& operator=(IndependentWrenchSource&&) = delete;
  ~IndependentWrenchSource() = default;

  // Writes the sim's latched value of each input into this instance's own registry, then latches.
  // Call it after the sample's run(1), when the latched values are that frame's.
  void copy_inputs() {
    for (const SimInput& input : sim_inputs_) {
      inputs_.write(input.name, *input.latched);
    }
    if (const std::string_view fault = inputs_.latch(); !fault.empty()) {
      throw Error("IndependentWrenchSource: " + plugin_path_ + "'s " + std::string{fault} +
                  " latched non-finite");
    }
  }

  [[nodiscard]] Wrench at(const VehicleState& vs, const EnvironmentSample& env) const noexcept {
    return source_->wrench_b(vs, env);
  }

  [[nodiscard]] const std::string& plugin_path() const noexcept { return plugin_path_; }

 private:
  struct SimInput {
    std::string name;
    const double* latched;
  };

  std::string plugin_path_;
  SignalRegistry registry_;
  InputRegistry inputs_;
  LoadedPlugin loaded_;
  WrenchSource* source_ = nullptr;
  std::vector<SimInput> sim_inputs_;
};

// D5 (Joe, 2026-10-11): per sample, for the force and for the moment separately,
// ||evaluated - total||_inf <= 1e-12 * ||total||_inf. Normwise, so a component crossing zero needs
// no absolute floor. A non-finite value on either side fails.
inline constexpr double kRelativeTolerance = 1e-12;

// The infinity-norm difference and scale of one vector pair.
struct VectorMiss {
  double difference = 0.0;
  double scale = 0.0;
  [[nodiscard]] double relative() const noexcept { return difference / scale; }
};

[[nodiscard]] inline VectorMiss vector_miss(const Eigen::Vector3d& evaluated,
                                            const Eigen::Vector3d& total) noexcept {
  return VectorMiss{.difference = (evaluated - total).cwiseAbs().maxCoeff(),
                    .scale = total.cwiseAbs().maxCoeff()};
}

[[nodiscard]] inline bool all_finite(const Wrench& w) noexcept {
  return w.force_b.allFinite() && w.moment_b.allFinite();
}

[[nodiscard]] inline ::testing::AssertionResult matches_relative(const Wrench& evaluated,
                                                                 const Wrench& total) {
  if (!all_finite(evaluated) || !all_finite(total)) {
    return ::testing::AssertionFailure()
           << "non-finite: evaluated force " << evaluated.force_b.transpose() << " moment "
           << evaluated.moment_b.transpose() << "; total force " << total.force_b.transpose()
           << " moment " << total.moment_b.transpose();
  }
  const VectorMiss force = vector_miss(evaluated.force_b, total.force_b);
  const VectorMiss moment = vector_miss(evaluated.moment_b, total.moment_b);
  if (force.difference <= kRelativeTolerance * force.scale &&
      moment.difference <= kRelativeTolerance * moment.scale) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure()
         << "force miss " << force.difference << " of " << force.scale << " (relative "
         << force.relative() << "), moment miss " << moment.difference << " of " << moment.scale
         << " (relative " << moment.relative() << "), tolerance " << kRelativeTolerance;
}

}  // namespace astra::test::nesc
