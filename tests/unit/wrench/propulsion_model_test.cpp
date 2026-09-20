// PropulsionModel (docs/architecture.md §7, ADR-0018): wrench_b()'s own assembly of
// PropulsionInputs -- mirrors aero_model_test.cpp's own coverage, plus R6's cross-check against
// AeroModel's identical true_airspeed_mps formula.
#include "astra/interfaces/propulsion_model.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"

namespace {

using astra::AeroInputs;
using astra::AeroModel;
using astra::Config;
using astra::EnvironmentSample;
using astra::PropulsionInputs;
using astra::PropulsionModel;
using astra::SignalRegistry;
using astra::StateVector;
using astra::StepContext;
using astra::VehicleState;
using astra::Wrench;

constexpr double kTolerance = 1e-12;
const astra::frames::Quaternion kIdentity{1.0, 0.0, 0.0, 0.0};

class StubPropulsionModel final : public PropulsionModel {
 public:
  void init(const Config& /*config*/, SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "stub_propulsion"; }

  [[nodiscard]] Wrench compute(const PropulsionInputs& inputs) const noexcept override {
    ++call_count;
    last_inputs = inputs;
    Wrench wrench;
    wrench.force_b(0) = 1.0;  // fixed, deliberately independent of every input field
    return wrench;
  }

  mutable std::uint32_t call_count = 0;
  mutable PropulsionInputs last_inputs{};
};

// A stub AeroModel used only for R6's cross-check below (not otherwise under test here --
// aero_model_test.cpp owns AeroModel's own assembly coverage).
class StubAeroModelForComparison final : public AeroModel {
 public:
  void init(const Config& /*config*/, SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "stub_aero_for_comparison"; }
  [[nodiscard]] Wrench compute(const AeroInputs& inputs) const noexcept override {
    last_inputs = inputs;
    return {};
  }
  mutable AeroInputs last_inputs{};
};

StateVector make_state(const Eigen::Vector3d& v_b) {
  StateVector y = StateVector::Zero();
  y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion) = kIdentity;
  y.segment<astra::state::kVelocitySize>(astra::state::kVelocity) = v_b;
  return y;
}

// R6: PropulsionInputs.true_airspeed_mps matches the identical formula/value as AeroInputs's own
// field for the same (vs, env), computed by two independent wrench_b() calls.
TEST(PropulsionModel, TrueAirspeedMatchesAeroModelsOwnFieldForTheSameState) {
  StubPropulsionModel propulsion;
  StubAeroModelForComparison aero;
  const StateVector y = make_state({80.0, -3.0, 2.0});
  const VehicleState vs{y};
  EnvironmentSample env;
  env.wind_n = Eigen::Vector3d{1.0, 0.0, 0.0};
  env.atmosphere.density_kg_m3 = 1.1;
  env.atmosphere.speed_of_sound_m_s = 330.0;
  env.atmosphere.temperature_k = 280.0;
  env.atmosphere.pressure_pa = 90000.0;

  static_cast<void>(propulsion.wrench_b(vs, env));
  static_cast<void>(
      aero.compute(AeroInputs{}));  // establishes aero.last_inputs exists; overwritten next
  static_cast<void>(aero.wrench_b(vs, env));

  ASSERT_EQ(propulsion.call_count, 1U);
  EXPECT_NEAR(aero.last_inputs.true_airspeed_mps, propulsion.last_inputs.true_airspeed_mps,
              kTolerance);
}

// R6: PropulsionInputs.atmosphere is a bitwise-equal passthrough of env.atmosphere -- all four
// AtmoState fields, not just density.
TEST(PropulsionModel, AtmospherePassthroughIsExactForEveryField) {
  StubPropulsionModel model;
  const StateVector y = make_state({60.0, 0.0, 0.0});
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.temperature_k = 288.15;
  env.atmosphere.pressure_pa = 101325.0;
  env.atmosphere.density_kg_m3 = 1.225;
  env.atmosphere.speed_of_sound_m_s = 340.294;

  static_cast<void>(model.wrench_b(vs, env));

  ASSERT_EQ(model.call_count, 1U);
  EXPECT_EQ(model.last_inputs.atmosphere.temperature_k, env.atmosphere.temperature_k);
  EXPECT_EQ(model.last_inputs.atmosphere.pressure_pa, env.atmosphere.pressure_pa);
  EXPECT_EQ(model.last_inputs.atmosphere.density_kg_m3, env.atmosphere.density_kg_m3);
  EXPECT_EQ(model.last_inputs.atmosphere.speed_of_sound_m_s, env.atmosphere.speed_of_sound_m_s);
}

// The identical unconditional isfinite guard AeroModel::wrench_b() has (ADR-0018), pinned here too
// since PropulsionModel provides its own separate wrench_b() override with its own duplicated
// guard logic, not a shared base-class implementation.
TEST(PropulsionModel, NonFiniteAtmosphereNeverCallsComputeAndReturnsAnAllNanWrench) {
  StubPropulsionModel model;
  const StateVector y = make_state({60.0, 0.0, 0.0});
  const VehicleState vs{y};
  EnvironmentSample env;  // atmosphere defaults to all-NaN

  const Wrench wrench = model.wrench_b(vs, env);

  EXPECT_EQ(model.call_count, 0U);
  EXPECT_FALSE(wrench.force_b.allFinite());
  EXPECT_FALSE(wrench.moment_b.allFinite());
}

// R7: compute() is unit-testable with a hand-built PropulsionInputs alone -- no VehicleState,
// EnvironmentSample, or wrench_b() involved at all.
TEST(PropulsionModel, ComputeIsUnitTestableWithHandBuiltInputsAndNoSimulator) {
  StubPropulsionModel model;
  PropulsionInputs inputs;
  inputs.true_airspeed_mps = 230.0;
  inputs.atmosphere.temperature_k = 223.15;
  inputs.atmosphere.pressure_pa = 26500.0;
  inputs.atmosphere.density_kg_m3 = 0.414;
  inputs.atmosphere.speed_of_sound_m_s = 299.5;

  const Wrench wrench = model.compute(inputs);

  EXPECT_EQ(model.call_count, 1U);
  EXPECT_EQ(model.last_inputs.true_airspeed_mps, 230.0);
  EXPECT_EQ(wrench.force_b(0), 1.0);
}

}  // namespace
