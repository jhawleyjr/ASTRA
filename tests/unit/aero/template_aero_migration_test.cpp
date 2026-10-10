// M7 PR 3b's migration check (docs/plans/m7-pr3b-earth-relative-rate.md, test D1), not part of
// the template's own tests. A copied aircraft that edits its aero.cpp or aero_math.hpp would fail
// it, and it proves only this PR's migration (ADR-0032).
//
// What it checks: aircraft/_template/physical/aero now damps air_data::omega_eb_b(vs, env), the
// body rate relative to the Earth. With the field the core sets on a flat or non-rotating Earth
// (+0.0 in every component), the template's wrench must equal the pre-migration wrench bit for bit,
// at three stated states, the last with -0.0 rates. The "before" expression is the pre-migration
// assembly, typed below, with the inertial vs.omega_b(), passed to the template's own pure
// compute_wrench(). The coefficients are the notional transport's
// (tests/python/test_plot_new_aircraft.py's FILL), the numbers the template's copy flies with.
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

#include "aircraft/_template/physical/aero/aero_math.hpp"
#include "astra/config/config.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/air_data.hpp"
#include "astra/util/frames.hpp"

namespace {

namespace air_data = astra::air_data;
namespace aero = astra::aircraft_template_aero_math;

using astra::AeroModel;
using astra::Config;
using astra::EnvironmentSample;
using astra::InputRegistry;
using astra::SignalRegistry;
using astra::StateVector;
using astra::VehicleState;

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

constexpr const char* kNotionalCoefficients =
    "wing_area_m2: 108.78945984\n"
    "wingspan_m: 28.86456\n"
    "mean_chord_m: 3.752088\n"
    "r_aero_b_x_m: 0.3556\n"
    "r_aero_b_y_m: 0.0\n"
    "r_aero_b_z_m: -1.6256\n"
    "cl_0: 0.2\n"
    "cl_alpha_per_rad: 4.3478260869565215\n"
    "cl_elevator_per_rad: 0.2\n"
    "cd_0: 0.021\n"
    "cd_k: 0.043\n"
    "cy_beta_per_rad: -1.0\n"
    "croll_beta_per_rad: -0.09\n"
    "croll_p_per_rad: -0.4\n"
    "croll_r_per_rad: 0.09\n"
    "croll_aileron_per_rad: 0.0743\n"
    "croll_rudder_per_rad: 0.01\n"
    "cm_alpha_per_rad: -0.6\n"
    "cm_q_per_rad: -27.0\n"
    "cm_elevator_per_rad: -0.8544\n"
    "cn_beta_per_rad: 0.26\n"
    "cn_r_per_rad: -0.35\n"
    "cn_rudder_per_rad: -0.2\n";

// The same Config values the plugin's init() reads, read here from the same Config, so the two
// sides share every coefficient bit.
aero::Coefficients coefficients(const Config& config) {
  aero::Coefficients c;
  c.wing_area_m2 = config.number("wing_area_m2");
  c.wingspan_m = config.number("wingspan_m");
  c.mean_chord_m = config.number("mean_chord_m");
  c.r_aero_b = Eigen::Vector3d(config.number("r_aero_b_x_m"), config.number("r_aero_b_y_m"),
                               config.number("r_aero_b_z_m"));
  c.cl_0 = config.number("cl_0");
  c.cl_alpha_per_rad = config.number("cl_alpha_per_rad");
  c.cl_elevator_per_rad = config.number("cl_elevator_per_rad");
  c.cd_0 = config.number("cd_0");
  c.cd_k = config.number("cd_k");
  c.cy_beta_per_rad = config.number("cy_beta_per_rad");
  c.croll_beta_per_rad = config.number("croll_beta_per_rad");
  c.croll_p_per_rad = config.number("croll_p_per_rad");
  c.croll_r_per_rad = config.number("croll_r_per_rad");
  c.croll_aileron_per_rad = config.number("croll_aileron_per_rad");
  c.croll_rudder_per_rad = config.number("croll_rudder_per_rad");
  c.cm_alpha_per_rad = config.number("cm_alpha_per_rad");
  c.cm_q_per_rad = config.number("cm_q_per_rad");
  c.cm_elevator_per_rad = config.number("cm_elevator_per_rad");
  c.cn_beta_per_rad = config.number("cn_beta_per_rad");
  c.cn_r_per_rad = config.number("cn_r_per_rad");
  c.cn_rudder_per_rad = config.number("cn_rudder_per_rad");
  return c;
}

struct Condition {
  const char* label;
  Eigen::Vector3d v_b;
  astra::frames::EulerAngles euler_nb;  // (psi, theta, phi)
  Eigen::Vector3d omega_b;
  double density_kg_m3;
  double elevator_rad;
  double aileron_rad;
  double rudder_rad;
};

// The pre-migration wrench_b() (aircraft/_template/physical/aero/aero.cpp before M7 PR 3b),
// typed: the same air_data:: calls in the same order, with the state's inertial omega_b.
astra::Wrench before_migration(const VehicleState& vs, const EnvironmentSample& env,
                               const Condition& condition, const aero::Coefficients& c) {
  const Eigen::Vector3d v_rel_b = air_data::relative_velocity_b(vs, env);
  aero::Inputs in;
  in.true_airspeed_mps = air_data::true_airspeed(v_rel_b);
  in.alpha_rad = air_data::angle_of_attack(v_rel_b);
  in.beta_rad = air_data::sideslip(v_rel_b);
  in.omega_b = vs.omega_b();
  in.dynamic_pressure_pa =
      air_data::dynamic_pressure(env.atmosphere.density_kg_m3, in.true_airspeed_mps);
  in.density_kg_m3 = env.atmosphere.density_kg_m3;
  in.elevator_rad = condition.elevator_rad;
  in.aileron_rad = condition.aileron_rad;
  in.rudder_rad = condition.rudder_rad;
  if (!aero::finite(in)) {
    return aero::non_finite_wrench();
  }
  return aero::compute_wrench(in, c);
}

TEST(TemplateAeroMigration, OnAFlatEarthTheWrenchIsTheInertialRateWrenchBitForBit) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load("aircraft/_template/physical/aero");
  auto* const plugin = dynamic_cast<AeroModel*>(loaded.model.get());
  ASSERT_NE(plugin, nullptr);
  const Config config = Config::from_string(kNotionalCoefficients, "template-aero-migration");
  SignalRegistry registry;
  plugin->init(config, registry);
  InputRegistry inputs;
  plugin->bind_inputs(inputs, registry);
  const aero::Coefficients c = coefficients(config);

  const std::array<Condition, 3> conditions{{
      {"W1, the copy's trim", Eigen::Vector3d{229.9360238725, 0.0, 5.42447469393253},
       astra::frames::EulerAngles{0.6, 0.023586859573503434, 0.0}, Eigen::Vector3d::Zero(), 0.4128,
       0.026674474137989946, 0.0, 0.0},
      {"W2, every term nonzero", 100.0 * Eigen::Vector3d{std::cos(0.1), std::sin(0.1), 0.0},
       astra::frames::EulerAngles{-2.5, 0.3, 0.2}, Eigen::Vector3d{0.05, -0.02, 0.03}, 1.0, 0.1,
       -0.05, 0.02},
      {"W3, signed zeros", Eigen::Vector3d{200.0, -0.0, 10.0},
       astra::frames::EulerAngles{-2.5, 0.3, 0.0}, Eigen::Vector3d::Constant(-0.0), 0.5, 0.0, 0.0,
       0.0},
  }};

  for (const Condition& condition : conditions) {
    SCOPED_TRACE(condition.label);
    inputs.write("controls.elevator", condition.elevator_rad);
    inputs.write("controls.aileron", condition.aileron_rad);
    inputs.write("controls.rudder", condition.rudder_rad);
    ASSERT_TRUE(inputs.latch().empty());

    StateVector y = StateVector::Zero();
    y.segment<astra::state::kVelocitySize>(astra::state::kVelocity) = condition.v_b;
    y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion) =
        astra::frames::quaternion_from_euler(condition.euler_nb);
    y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate) = condition.omega_b;
    const VehicleState vs{y};
    EnvironmentSample env;  // still air
    env.atmosphere.density_kg_m3 = condition.density_kg_m3;
    env.omega_ie_b = Eigen::Vector3d::Zero();  // the core's flat-Earth value: +0.0 bit for bit

    const astra::Wrench after = plugin->wrench_b(vs, env);
    const astra::Wrench before = before_migration(vs, env, condition, c);
    ASSERT_TRUE(before.force_b.allFinite() && before.moment_b.allFinite());
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(bits(after.force_b(i)), bits(before.force_b(i)))
          << "force_b " << i << ": " << after.force_b(i) << " vs " << before.force_b(i);
      EXPECT_EQ(bits(after.moment_b(i)), bits(before.moment_b(i)))
          << "moment_b " << i << ": " << after.moment_b(i) << " vs " << before.moment_b(i);
    }
  }
}

}  // namespace
