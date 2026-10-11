// This aircraft's physical/aero plugin (docs/plans/pr4-notional-aero-turbofan.md, R9): the
// un-edited template's own config (configs/cruise.yaml) carries every coefficient zeroed and
// TODO-marked. This pins the intended failure mode: copying _template and never touching the
// numbers cannot silently fly -- init() throws a ConfigError naming the exact field still at its
// TODO value (wing_area_m2, checked first). Mirrors mass_test.cpp, including the aircraft path from
// ASTRA_THIS_AIRCRAFT.
//
// The last two tests pin which body rate this aero damps (ADR-0032, M7 PR 3b): the rate relative to
// the Earth, air_data::omega_eb_b(vs, env), which the core supplies in every RK4 stage.
#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/frames.hpp"

namespace {

using astra::AeroModel;
using astra::Config;
using astra::ConfigError;

TEST(AircraftAero, ZeroedConfigThrowsConfigErrorNamingTheTodoField) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load(ASTRA_THIS_AIRCRAFT "/physical/aero");
  auto* const aero = dynamic_cast<AeroModel*>(loaded.model.get());
  astra::SignalRegistry registry;

  try {
    // The template's configs/cruise.yaml, un-edited.
    aero->init(Config::from_string("wing_area_m2: 0.0\n"
                                   "wingspan_m: 0.0\n"
                                   "mean_chord_m: 0.0\n"
                                   "r_aero_b_x_m: 0.0\n"
                                   "r_aero_b_y_m: 0.0\n"
                                   "r_aero_b_z_m: 0.0\n"
                                   "cl_0: 0.0\n"
                                   "cl_alpha_per_rad: 0.0\n"
                                   "cl_elevator_per_rad: 0.0\n"
                                   "cd_0: 0.0\n"
                                   "cd_k: 0.0\n"
                                   "cy_beta_per_rad: 0.0\n"
                                   "croll_beta_per_rad: 0.0\n"
                                   "croll_p_per_rad: 0.0\n"
                                   "croll_r_per_rad: 0.0\n"
                                   "croll_aileron_per_rad: 0.0\n"
                                   "croll_rudder_per_rad: 0.0\n"
                                   "cm_alpha_per_rad: 0.0\n"
                                   "cm_q_per_rad: 0.0\n"
                                   "cm_elevator_per_rad: 0.0\n"
                                   "cn_beta_per_rad: 0.0\n"
                                   "cn_r_per_rad: 0.0\n"
                                   "cn_rudder_per_rad: 0.0\n",
                                   "template-aero-fixture"),
               registry);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "wing_area_m2");
  }
}

// Codex general-review finding (turbofan.cpp's density_ref_kgm3, same helper here): `value > 0.0`
// alone is true for +Inf, so checked_positive() used to accept an infinite wing_area_m2. Fixed:
// checked_positive() now also requires std::isfinite().
TEST(AircraftAero, InfiniteWingAreaThrowsConfigError) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load(ASTRA_THIS_AIRCRAFT "/physical/aero");
  auto* const aero = dynamic_cast<AeroModel*>(loaded.model.get());
  astra::SignalRegistry registry;

  try {
    aero->init(Config::from_string("wing_area_m2: .inf\n"
                                   "wingspan_m: 1.0\n"
                                   "mean_chord_m: 1.0\n"
                                   "r_aero_b_x_m: 0.0\n"
                                   "r_aero_b_y_m: 0.0\n"
                                   "r_aero_b_z_m: 0.0\n"
                                   "cl_0: 0.0\n"
                                   "cl_alpha_per_rad: 0.0\n"
                                   "cl_elevator_per_rad: 0.0\n"
                                   "cd_0: 0.0\n"
                                   "cd_k: 0.0\n"
                                   "cy_beta_per_rad: 0.0\n"
                                   "croll_beta_per_rad: 0.0\n"
                                   "croll_p_per_rad: 0.0\n"
                                   "croll_r_per_rad: 0.0\n"
                                   "croll_aileron_per_rad: 0.0\n"
                                   "croll_rudder_per_rad: 0.0\n"
                                   "cm_alpha_per_rad: 0.0\n"
                                   "cm_q_per_rad: 0.0\n"
                                   "cm_elevator_per_rad: 0.0\n"
                                   "cn_beta_per_rad: 0.0\n"
                                   "cn_r_per_rad: 0.0\n"
                                   "cn_rudder_per_rad: 0.0\n",
                                   "template-aero-infinite-wing-area-fixture"),
               registry);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "wing_area_m2");
  }
}

// ADR-0032: the contract a copy keeps. These two tests compare the plugin only with itself or with
// finiteness, so a copy that edits its own coefficients or math still passes them while it damps
// air_data::omega_eb_b. The coefficients are illustrative (the notional transport's), not this
// aircraft's data.
constexpr const char* kIllustrativeCoefficients =
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

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

astra::StateVector rate_state(const Eigen::Vector3d& v_b,
                              const astra::frames::EulerAngles& euler_nb,
                              const Eigen::Vector3d& omega_b) {
  astra::StateVector y = astra::StateVector::Zero();
  y.segment<astra::state::kVelocitySize>(astra::state::kVelocity) = v_b;
  y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion) =
      astra::frames::quaternion_from_euler(euler_nb);
  y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate) = omega_b;
  return y;
}

astra::EnvironmentSample still_air(double density_kg_m3) {
  astra::EnvironmentSample env;
  env.atmosphere.density_kg_m3 = density_kg_m3;
  return env;
}

bool same_bits(const astra::Wrench& a, const astra::Wrench& b) {
  for (int i = 0; i < 3; ++i) {
    if (bits(a.force_b(i)) != bits(b.force_b(i)) || bits(a.moment_b(i)) != bits(b.moment_b(i))) {
      return false;
    }
  }
  return true;
}

// D2: with omega_ie_b equal to the state's own rate (a vehicle turning exactly with the Earth),
// the wrench is the zero-rate wrench bit for bit, since omega_b - omega_b is exactly +0.0. The
// control shows the rates do move this wrench, so the equality is not vacuous.
TEST(AircraftAero, DampsTheBodyRateRelativeToTheEarth) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load(ASTRA_THIS_AIRCRAFT "/physical/aero");
  auto* const aero = dynamic_cast<AeroModel*>(loaded.model.get());
  ASSERT_NE(aero, nullptr);
  astra::SignalRegistry registry;
  aero->init(Config::from_string(kIllustrativeCoefficients, "template-aero-rate-fixture"),
             registry);
  astra::InputRegistry inputs;
  aero->bind_inputs(inputs, registry);
  inputs.write("controls.elevator", 0.1);
  inputs.write("controls.aileron", -0.05);
  inputs.write("controls.rudder", 0.02);
  ASSERT_TRUE(inputs.latch().empty());

  const Eigen::Vector3d v_b = 100.0 * Eigen::Vector3d{std::cos(0.1), std::sin(0.1), 0.0};
  const astra::frames::EulerAngles euler_nb{-2.5, 0.3, 0.2};
  const Eigen::Vector3d omega_b{0.05, -0.02, 0.03};

  const astra::StateVector rotating = rate_state(v_b, euler_nb, omega_b);
  astra::EnvironmentSample turning_with_the_earth = still_air(1.0);
  turning_with_the_earth.omega_ie_b = omega_b;
  const astra::Wrench relative =
      aero->wrench_b(astra::VehicleState{rotating}, turning_with_the_earth);

  const astra::StateVector still = rate_state(v_b, euler_nb, Eigen::Vector3d::Zero());
  astra::EnvironmentSample flat = still_air(1.0);
  flat.omega_ie_b = Eigen::Vector3d::Zero();
  const astra::Wrench zero_rate = aero->wrench_b(astra::VehicleState{still}, flat);
  EXPECT_TRUE(same_bits(relative, zero_rate));

  const astra::Wrench rotating_on_a_flat_earth =
      aero->wrench_b(astra::VehicleState{rotating}, flat);
  EXPECT_FALSE(same_bits(rotating_on_a_flat_earth, zero_rate)) << "the rates must move the wrench";
}

// D3: a hand-built EnvironmentSample whose omega_ie_b is left at its NaN default gives a non-finite
// wrench, never a silent non-rotating Earth. Setting it (+0.0, a flat Earth) gives a finite one.
TEST(AircraftAero, AnUnsetEarthRateGivesANonFiniteWrench) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load(ASTRA_THIS_AIRCRAFT "/physical/aero");
  auto* const aero = dynamic_cast<AeroModel*>(loaded.model.get());
  ASSERT_NE(aero, nullptr);
  astra::SignalRegistry registry;
  aero->init(Config::from_string(kIllustrativeCoefficients, "template-aero-nan-fixture"), registry);
  astra::InputRegistry inputs;
  aero->bind_inputs(inputs, registry);
  inputs.write("controls.elevator", 0.026674474137989946);
  inputs.write("controls.aileron", 0.0);
  inputs.write("controls.rudder", 0.0);
  ASSERT_TRUE(inputs.latch().empty());

  const astra::StateVector trim = rate_state(
      Eigen::Vector3d{229.9360238725, 0.0, 5.42447469393253},
      astra::frames::EulerAngles{0.6, 0.023586859573503434, 0.0}, Eigen::Vector3d::Zero());
  const astra::VehicleState vs{trim};

  const astra::Wrench unset = aero->wrench_b(vs, still_air(0.4128));
  EXPECT_FALSE(unset.force_b.allFinite());
  EXPECT_FALSE(unset.moment_b.allFinite());

  astra::EnvironmentSample set = still_air(0.4128);
  set.omega_ie_b = Eigen::Vector3d::Zero();
  const astra::Wrench finite = aero->wrench_b(vs, set);
  EXPECT_TRUE(finite.force_b.allFinite());
  EXPECT_TRUE(finite.moment_b.allFinite());
}

}  // namespace
