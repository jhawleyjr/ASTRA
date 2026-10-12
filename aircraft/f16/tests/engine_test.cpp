// The F-16 engine plugin against its source, NASA's F16_prop.dml (M7 PR 6c; R3;
// docs/plans/m7-pr6c-f16-mass-propulsion.md, with the design in docs/plans/m7-pr6b-f16-aircraft.md,
// "Propulsion"). The oracle is the file's own checkData, extracted by extract_f16_checkdata.py into
// f16_checkdata.hpp, which shares no code with the table generator. The map points and the
// extrapolation's expected values come from table entries transcribed by hand, each with its line.
//
// Pre-registered in the plan, and kept here:
// - CheckData: each of the 9 shots through wrench_b, with throttle = PWR / 100, altitude_m = the
//   shot's altitude in m, and v_b = (Mach * a, 0, 0) at the test's own speed of sound a. The
//   thrust, converted back to lbf, passes |FEX - FEX_good| < tol at the shot's own tol (strict,
//   absolute: the file's criterion), and the other five outputs are exactly zero. The tightest is
//   shot 8 (F16_prop.dml:856): its published 9298.8926 lbf lies 5.69e-4 lbf from the file's own
//   internal FEX, 9298.892031035, against a tol of 6e-4, so it passes only if the port reproduces
//   the file's arithmetic to 3.1e-5 lbf.
// - The map points and the extrapolation to 1e-12 relative; the clipped edges bit for bit.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include "../hardware/propulsion/engine/engine_math.hpp"
#include "astra/config/config.hpp"
#include "astra/interfaces/propulsion_model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/units.hpp"
#include "f16_checkdata.hpp"

namespace {

namespace checkdata = astra::aircraft_f16_checkdata;
namespace engine = astra::aircraft_f16_engine_math;
namespace units = astra::units;

// The test's own speed of sound, m/s. The engine reads only Mach = V / a, so any positive value
// serves.
constexpr double kSpeedOfSound = 340.0;

// The three tables' entries at Mach 0 and 0 ft, transcribed by hand, lbf.
constexpr double kIdleLbf = 1060.0;  // T_IDLE, F16_prop.dml:272
constexpr double kMilLbf = 12680.0;  // T_MIL, F16_prop.dml:296
constexpr double kMaxLbf = 20000.0;  // T_MAX, F16_prop.dml:320

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

double input(const checkdata::Shot& shot, std::string_view name) {
  for (const checkdata::Signal& signal : shot.inputs) {
    if (signal.name == name) {
      return signal.value;
    }
  }
  ADD_FAILURE() << shot.name << " has no input " << name;
  return std::nan("");
}

// The wrench component each of the file's six outputs names, in SI.
double component(const astra::Wrench& w, std::string_view output) {
  if (output == "thrustBodyForce_X") {
    return w.force_b.x();
  }
  if (output == "thrustBodyForce_Y") {
    return w.force_b.y();
  }
  if (output == "thrustBodyForce_Z") {
    return w.force_b.z();
  }
  if (output == "thrustBodyMoment_Roll") {
    return w.moment_b.x();
  }
  if (output == "thrustBodyMoment_Pitch") {
    return w.moment_b.y();
  }
  if (output == "thrustBodyMoment_Yaw") {
    return w.moment_b.z();
  }
  ADD_FAILURE() << "no wrench component for output " << output;
  return std::nan("");
}

bool all_nan(const astra::Wrench& w) {
  return w.force_b.array().isNaN().all() && w.moment_b.array().isNaN().all();
}

// The loaded plugin with its write seam, as Vehicle would hold it.
class EngineUnderTest {
 public:
  EngineUnderTest()
      : loaded_(loader_.load(ASTRA_THIS_AIRCRAFT "/hardware/propulsion/engine")),
        engine_(dynamic_cast<astra::PropulsionModel*>(loaded_.model.get())) {
    engine_->init(astra::Config::from_string("{}", "f16-engine-test"), registry_);
    engine_->bind_inputs(inputs_, registry_);
  }

  [[nodiscard]] const astra::LoadedPlugin& loaded() const { return loaded_; }
  [[nodiscard]] const astra::PropulsionModel& engine() const { return *engine_; }
  [[nodiscard]] const astra::InputRegistry& inputs() const { return inputs_; }
  [[nodiscard]] const astra::SignalRegistry& registry() const { return registry_; }

  // The throttle through the write seam, a true airspeed along the body x axis (m/s), identity
  // attitude and no wind.
  astra::Wrench at(double throttle, const astra::EnvironmentSample& env, double airspeed) {
    inputs_.write("controls.throttle", throttle);
    EXPECT_TRUE(inputs_.latch().empty());
    astra::StateVector y = astra::StateVector::Zero();
    y(astra::state::kVelocity) = airspeed;
    y(astra::state::kQuaternion) = 1.0;
    return engine_->wrench_b(astra::VehicleState{y}, env);
  }

  // altitude in m, Mach at the test's speed of sound.
  astra::Wrench at(double throttle, double altitude, double mach) {
    astra::EnvironmentSample env;
    env.atmosphere.speed_of_sound_m_s = kSpeedOfSound;
    env.altitude_m = altitude;
    return at(throttle, env, mach * kSpeedOfSound);
  }

 private:
  astra::PluginLoader loader_{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded_;
  astra::PropulsionModel* engine_;
  astra::SignalRegistry registry_;
  astra::InputRegistry inputs_;
};

TEST(F16Engine, DescriptorAndWriteSeamName) {
  EngineUnderTest plugin;
  EXPECT_STREQ(plugin.loaded().descriptor->category, "hardware");
  EXPECT_STREQ(plugin.loaded().descriptor->name, "f16_engine");
  EXPECT_EQ(plugin.loaded().model->name(), "engine");
  EXPECT_EQ(plugin.inputs().names(), (std::vector<std::string_view>{"controls.throttle"}));
  // The write seam publishes the throttle, dimensionless, and the plugin publishes nothing else.
  ASSERT_EQ(plugin.registry().signals().size(), 1U);
  EXPECT_EQ(plugin.registry().signals()[0].name, "controls.throttle");
  EXPECT_EQ(plugin.registry().signals()[0].unit, "1");
  // Mach needs the atmosphere's speed of sound: PropulsionModel's default, kept.
  const astra::EnvironmentRequirements needs = plugin.engine().requires_environment();
  EXPECT_TRUE(needs.atmosphere);
  EXPECT_FALSE(needs.wind);
}

TEST(F16Engine, CheckData) {
  EngineUnderTest plugin;
  double worst_ratio = 0.0;  // |FEX - FEX_good| / tol
  int worst_line = 0;
  for (const checkdata::Shot& shot : checkdata::kPropShots) {
    SCOPED_TRACE(::testing::Message() << shot.name << " (F16_prop.dml:" << shot.line << ")");
    const double throttle = input(shot, "powerLeverAngle") / 100.0;
    const double altitude = units::feet_to_meters(input(shot, "altitudeMSL"));
    const astra::Wrench w = plugin.at(throttle, altitude, input(shot, "mach"));

    ASSERT_EQ(shot.outputs.size(), 6U);
    for (const checkdata::Output& output : shot.outputs) {
      if (output.name == "thrustBodyForce_X") {
        const double fex = units::newtons_to_pound_force(w.force_b.x());
        const double error = std::abs(fex - output.value);
        EXPECT_LT(error, output.tol) << fex << " lbf against " << output.value;
        if (error / output.tol > worst_ratio) {
          worst_ratio = error / output.tol;
          worst_line = shot.line;
        }
      } else {
        // Zero in the file, exactly zero here.
        EXPECT_EQ(component(w, output.name), 0.0) << output.name;
      }
    }
  }
  std::cout << "F16Engine.CheckData: largest |FEX - FEX_good| / tol over 9 shots = " << worst_ratio
            << " (the shot at F16_prop.dml:" << worst_line << ")\n";
}

// R3: the throttle maps to the power lever angle in one place, PWR = 100 * throttle. At Mach 0 and
// 0 ft, throttle 0, 0.5 (MIL) and 1 (MAX) give the three tables' entries there.
TEST(F16Engine, MapPinnedAtIdleMilAndMax) {
  EngineUnderTest plugin;
  for (const auto& [throttle, lbf] :
       {std::pair{0.0, kIdleLbf}, std::pair{0.5, kMilLbf}, std::pair{1.0, kMaxLbf}}) {
    const double expected = units::pound_force_to_newtons(lbf);
    const double thrust = plugin.at(throttle, 0.0, 0.0).force_b.x();
    EXPECT_LE(std::abs(thrust - expected), 1e-12 * std::abs(expected))
        << "throttle " << throttle << ": " << thrust << " N against " << expected;
  }
}

// Q15's clip on both axes of all three tables. Through the math seam, PWR 0, 50 and 100 give
// T_IDLE, T_MIL and T_MAX at their weights, so each table is reached. Past an edge equals the edge,
// bit for bit: Mach -0.5 and 2.0, and -1,000 ft and 60,000 ft. Then through wrench_b, -1,000 ft
// equals 0 ft, and Mach 2.0 at 30,000 ft equals Mach 1.0 there (case 12's clip).
TEST(F16Engine, EveryEdgeClips) {
  const auto thrust = [](double pwr, double altitude_ft, double mach) {
    return engine::thrust(
        {.pwr = pwr, .altitude = units::feet_to_meters(altitude_ft), .mach = mach});
  };
  for (const double pwr : {0.0, 50.0, 100.0}) {
    for (const double altitude_ft : {0.0, 23507.0, 50000.0}) {
      EXPECT_EQ(bits(thrust(pwr, altitude_ft, -0.5)), bits(thrust(pwr, altitude_ft, 0.0)))
          << "PWR " << pwr << ", " << altitude_ft << " ft, below Mach 0";
      EXPECT_EQ(bits(thrust(pwr, altitude_ft, 2.0)), bits(thrust(pwr, altitude_ft, 1.0)))
          << "PWR " << pwr << ", " << altitude_ft << " ft, above Mach 1";
    }
    for (const double mach : {0.0, 0.625, 1.0}) {
      EXPECT_EQ(bits(thrust(pwr, -1000.0, mach)), bits(thrust(pwr, 0.0, mach)))
          << "PWR " << pwr << ", Mach " << mach << ", below 0 ft";
      EXPECT_EQ(bits(thrust(pwr, 60000.0, mach)), bits(thrust(pwr, 50000.0, mach)))
          << "PWR " << pwr << ", Mach " << mach << ", above 50,000 ft";
    }
  }

  EngineUnderTest plugin;
  const double thirty_thousand_feet = units::feet_to_meters(30000.0);
  for (const double throttle : {0.0, 0.5, 1.0}) {
    EXPECT_EQ(bits(plugin.at(throttle, units::feet_to_meters(-1000.0), 0.4).force_b.x()),
              bits(plugin.at(throttle, 0.0, 0.4).force_b.x()))
        << "throttle " << throttle;
    EXPECT_EQ(bits(plugin.at(throttle, thirty_thousand_feet, 2.0).force_b.x()),
              bits(plugin.at(throttle, thirty_thousand_feet, 1.0).force_b.x()))
        << "throttle " << throttle;
  }
}

// PR 6b's decision 4: PWR is not saturated, so a throttle outside 0 to 1 extrapolates the build-up
// from the same entries. A clamp to 0 to 1 would give 1060 and 20000 lbf.
TEST(F16Engine, ThrottleOutsideZeroToOneExtrapolates) {
  constexpr double kBelowLbf = kIdleLbf + (-10.0) * (kMilLbf - kIdleLbf) / 50.0;  // PWR -10
  constexpr double kAboveLbf = kMilLbf + 70.0 * (kMaxLbf - kMilLbf) / 50.0;       // PWR 120
  static_assert(kBelowLbf == -1264.0 && kAboveLbf == 22928.0);

  EngineUnderTest plugin;
  for (const auto& [throttle, lbf] : {std::pair{-0.1, kBelowLbf}, std::pair{1.2, kAboveLbf}}) {
    const double expected = units::pound_force_to_newtons(lbf);
    const double thrust = plugin.at(throttle, 0.0, 0.0).force_b.x();
    EXPECT_LE(std::abs(thrust - expected), 1e-12 * std::abs(expected))
        << "throttle " << throttle << ": " << thrust << " N against " << expected;
  }
}

// A hand-built EnvironmentSample that never set altitude_m keeps its NaN default (ADR-0033), and
// every component of the wrench is NaN: never sea-level thrust. A NaN speed of sound does the same,
// and so does a zero one (M7 PR 6c, Deviation 4), whose infinite Mach would otherwise clip to
// Mach 1's thrust.
TEST(F16Engine, AnUnsetAltitudeGivesANonFiniteWrench) {
  EngineUnderTest plugin;
  constexpr double kAirspeed = 100.0;  // m/s, nonzero

  astra::EnvironmentSample unset_altitude;
  unset_altitude.atmosphere.speed_of_sound_m_s = kSpeedOfSound;
  const astra::Wrench at_unset_altitude = plugin.at(0.5, unset_altitude, kAirspeed);
  EXPECT_TRUE(all_nan(at_unset_altitude))
      << at_unset_altitude.force_b.transpose() << " / " << at_unset_altitude.moment_b.transpose();

  astra::EnvironmentSample nan_speed_of_sound;  // the atmosphere at its all-NaN default
  nan_speed_of_sound.altitude_m = units::feet_to_meters(10000.0);
  const astra::Wrench at_nan_speed_of_sound = plugin.at(0.5, nan_speed_of_sound, kAirspeed);
  EXPECT_TRUE(all_nan(at_nan_speed_of_sound)) << at_nan_speed_of_sound.force_b.transpose() << " / "
                                              << at_nan_speed_of_sound.moment_b.transpose();

  astra::EnvironmentSample zero_speed_of_sound;
  zero_speed_of_sound.altitude_m = units::feet_to_meters(10000.0);
  zero_speed_of_sound.atmosphere.speed_of_sound_m_s = 0.0;
  const astra::Wrench at_zero_speed_of_sound = plugin.at(0.5, zero_speed_of_sound, kAirspeed);
  EXPECT_TRUE(all_nan(at_zero_speed_of_sound))
      << at_zero_speed_of_sound.force_b.transpose() << " / "
      << at_zero_speed_of_sound.moment_b.transpose();
}

}  // namespace
