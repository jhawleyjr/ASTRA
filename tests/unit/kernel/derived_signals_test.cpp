// M5 PR 3 (ADR-0026; docs/plans/m5-tooling.md Q13, R10): the v<i>.derived.* signals. The exact
// published list and units; none without physics; R10's bitwise equality with the exchange
// recorder's sim_to_sw request at every exchange frame (ax ay az included, so the previous-frame
// rule matches); the previous-frame validity rule on its own (frame 0, a probe-latched mass, a
// frame jump); independence from whether a transport is configured; one set per physics vehicle.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/kernel/exchange_recorder.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/frames.hpp"
#include "icd/astra_icd.h"

namespace {

using astra::Config;
using astra::Simulation;

// ADR-0026's list, written out here rather than read from the stage: the ICD truth fields in
// astra_sim_to_sw_v1 order, then v_n, each with its ICD-comment SI unit.
const std::vector<std::pair<std::string, std::string>> kExpectedFields = {
    {"phi", "rad"},   {"theta", "rad"},      {"psi", "rad"},    {"p", "rad/s"},
    {"q", "rad/s"},   {"r", "rad/s"},        {"u", "m/s"},      {"v", "m/s"},
    {"w", "m/s"},     {"alpha", "rad"},      {"beta", "rad"},   {"tas", "m/s"},
    {"cas", "m/s"},   {"mach", "1"},         {"altitude", "m"}, {"vertical_speed", "m/s"},
    {"lat", "rad"},   {"lon", "rad"},        {"ax", "m/s^2"},   {"ay", "m/s^2"},
    {"az", "m/s^2"},  {"status_flags", "1"}, {"v_n_0", "m/s"},  {"v_n_1", "m/s"},
    {"v_n_2", "m/s"},
};

// The 21 double truth fields of astra_sim_to_sw_v1, by name, for the bitwise comparison with the
// recorder (status_flags is compared separately, as an integer).
struct DoubleField {
  std::string_view name;
  double astra_sim_to_sw_v1::*member;
};
constexpr std::array<DoubleField, 21> kDoubleFields = {{
    {"phi", &astra_sim_to_sw_v1::phi},
    {"theta", &astra_sim_to_sw_v1::theta},
    {"psi", &astra_sim_to_sw_v1::psi},
    {"p", &astra_sim_to_sw_v1::p},
    {"q", &astra_sim_to_sw_v1::q},
    {"r", &astra_sim_to_sw_v1::r},
    {"u", &astra_sim_to_sw_v1::u},
    {"v", &astra_sim_to_sw_v1::v},
    {"w", &astra_sim_to_sw_v1::w},
    {"alpha", &astra_sim_to_sw_v1::alpha},
    {"beta", &astra_sim_to_sw_v1::beta},
    {"tas", &astra_sim_to_sw_v1::tas},
    {"cas", &astra_sim_to_sw_v1::cas},
    {"mach", &astra_sim_to_sw_v1::mach},
    {"altitude", &astra_sim_to_sw_v1::altitude},
    {"vertical_speed", &astra_sim_to_sw_v1::vertical_speed},
    {"lat", &astra_sim_to_sw_v1::lat},
    {"lon", &astra_sim_to_sw_v1::lon},
    {"ax", &astra_sim_to_sw_v1::ax},
    {"ay", &astra_sim_to_sw_v1::ay},
    {"az", &astra_sim_to_sw_v1::az},
}};

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

double derived(const Simulation& sim, std::string_view field, std::string_view vehicle = "v0") {
  const std::string name = std::string{vehicle} + ".derived." + std::string{field};
  const double* value = sim.registry().find(name);
  EXPECT_NE(value, nullptr) << name;
  return value == nullptr ? std::numeric_limits<double>::quiet_NaN() : *value;
}

std::uint32_t status(const Simulation& sim, std::string_view vehicle = "v0") {
  return static_cast<std::uint32_t>(derived(sim, "status_flags", vehicle));
}

// A flat-Earth, no-wrench fixture (simulation_transport_test.cpp's own), optionally with a
// loopback transport at `divisor`.
std::string flat_yaml(bool transport, std::uint32_t divisor = 10) {
  std::ostringstream out;
  out << "sim:\n  dt: 0.001\n  frames: 100\n"
         "vehicle:\n  models:\n    - { plugin: tests/plugins/input_consumer }\n"
         "  initial_controls:\n    controls:\n      elevator: 0.0\n"
         "physical:\n"
         "  earth: plugins/physical/earth/flat\n"
         "  gravity: plugins/physical/gravity/constant\n"
         "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
         "izz: 1.0 }\n"
         "initial:\n  state: { p_n: [0.0, 0.0, -100.0], v_b: [60.0, 0.0, 0.0] }\n";
  if (transport) {
    out << "transport:\n  plugin: plugins/sim/transport/loopback\n  divisor: " << divisor
        << "\n  commands: { elevator: controls.elevator }\n"
           "  limits: { elevator: [-1.0, 1.0], aileron: [-1.0, 1.0], rudder: [-1.0, 1.0], "
           "throttle: [0.0, 1.0] }\n"
           "  peer:\n    commands: { elevator: 0.01 }\n";
  }
  return out.str();
}

Simulation build_flat(bool transport, std::uint32_t divisor = 10) {
  return Simulation::from_config(Config::from_string(flat_yaml(transport, divisor), "flat"),
                                 ASTRA_TEST_PLUGIN_DIR);
}

// aircraft/notional_transport/configs/cruise.yaml verbatim (Dryden, isa1976, wgs84_rotating: every
// ICD field is valid, lat/lon included), optionally with a loopback transport whose peer replies
// with the file's own trim literals (simulation_transport_test.cpp's kCruiseTrimTransport).
Simulation build_cruise(bool transport, std::uint32_t divisor = 10) {
  std::string text =
      read_file(std::string{ASTRA_AIRCRAFT_DIR} + "/notional_transport/configs/cruise.yaml");
  if (transport) {
    text +=
        "\ntransport:\n"
        "  plugin: plugins/sim/transport/loopback\n"
        "  divisor: " +
        std::to_string(divisor) +
        "\n"
        "  commands: { elevator: controls.elevator, aileron: controls.aileron, rudder: "
        "controls.rudder, throttle: [controls.throttle, controls.throttle] }\n"
        "  limits: { elevator: [-0.5, 0.5], aileron: [-0.5, 0.5], rudder: [-0.5, 0.5], "
        "throttle: [0.0, 1.0] }\n"
        "  peer:\n"
        "    commands: { elevator: 0.026674474137989946, aileron: 0.0, rudder: 0.0, "
        "throttle: [0.4985979490507746, 0.4985979490507746] }\n";
  }
  return Simulation::from_config(Config::from_string(text, "cruise"), ASTRA_TEST_PLUGIN_DIR);
}

// Every v<i>.derived.* field equals the request's field bitwise.
void expect_equals_request(const Simulation& sim, const astra_sim_to_sw_v1& request) {
  for (const DoubleField& field : kDoubleFields) {
    EXPECT_EQ(bits(derived(sim, field.name)), bits(request.*field.member))
        << "frame " << request.frame << " field " << field.name;
  }
  EXPECT_EQ(status(sim), request.status_flags) << "frame " << request.frame;
}

// ---------------------------------------------------------------------------------------------
// What is published.

TEST(DerivedSignals, APhysicsConfigPublishesExactlyTheTwentyFiveFieldsInOrderWithUnits) {
  const Simulation sim = build_flat(false);
  std::vector<std::pair<std::string, std::string>> published;
  for (const astra::Signal& signal : sim.registry().signals()) {
    if (signal.name.find(".derived.") != std::string::npos) {
      EXPECT_TRUE(signal.name.starts_with("v0.derived.")) << signal.name;
      EXPECT_EQ(signal.divisor, 1U) << signal.name;
      published.emplace_back(signal.name.substr(std::string_view{"v0.derived."}.size()),
                             signal.unit);
    }
  }
  EXPECT_EQ(published, kExpectedFields);
}

TEST(DerivedSignals, AConfigWithoutPhysicsPublishesNone) {
  const Simulation sim = Simulation::from_config(
      Config::from_string("sim:\n  dt: 0.001\n  frames: 3\nvehicle:\n  models:\n"
                          "    - { plugin: tests/plugins/dummy }\n",
                          "no physics"),
      ASTRA_TEST_PLUGIN_DIR);
  for (const astra::Signal& signal : sim.registry().signals()) {
    EXPECT_EQ(signal.name.find(".derived."), std::string::npos) << signal.name;
  }
}

// ---------------------------------------------------------------------------------------------
// R10: the derived signals are the ICD truth the exchange publishes, frame by frame.

TEST(DerivedSignals, EqualTheRecordedRequestBitwiseAtEveryExchangeFrameOnWgs84WithDryden) {
  Simulation sim = build_cruise(true, 1);
  constexpr std::uint64_t kFrames = 200;
  for (std::uint64_t k = 0; k < kFrames; ++k) {
    ASSERT_FALSE(sim.step()) << "frame " << k;
    const auto requests = sim.exchange_recorder()->requests();
    ASSERT_EQ(requests.size(), k + 1);
    ASSERT_EQ(requests.back().frame, k);
    expect_equals_request(sim, requests.back());
  }
  // The comparison covered valid values, not only defined zeros: geodetic, subsonic, accel valid.
  EXPECT_EQ(status(sim), 0U);
  EXPECT_NE(derived(sim, "lat"), 0.0);
  EXPECT_NE(derived(sim, "cas"), 0.0);
  EXPECT_NE(derived(sim, "az"), 0.0);
}

TEST(DerivedSignals, EqualTheRecordedRequestBitwiseOnFlatEarthAtDivisorTen) {
  Simulation sim = build_flat(true, 10);
  for (std::uint64_t k = 0; k < 100; ++k) {
    ASSERT_FALSE(sim.step()) << "frame " << k;
    if (k % 10 == 0) {
      const auto requests = sim.exchange_recorder()->requests();
      ASSERT_EQ(requests.back().frame, k);
      expect_equals_request(sim, requests.back());
    }
  }
  EXPECT_EQ(status(sim) & ASTRA_ICD_STATUS_LATLON_NOT_MODELED, ASTRA_ICD_STATUS_LATLON_NOT_MODELED);
  // No atmosphere configured: mach and cas are the defined 0 with the bit set.
  EXPECT_EQ(status(sim) & ASTRA_ICD_STATUS_ATMOSPHERE_INVALID, ASTRA_ICD_STATUS_ATMOSPHERE_INVALID);
  EXPECT_EQ(bits(derived(sim, "mach")), bits(0.0));
  EXPECT_EQ(bits(derived(sim, "cas")), bits(0.0));
}

// The stage reads state and never depends on the transport: with trim replies (a trajectory
// bit-identical to no transport, SimulationTransport's R8 test), every derived value matches at
// every frame, exchange frame or not.
TEST(DerivedSignals, AreTheSameWithAndWithoutATransport) {
  Simulation plain = build_cruise(false);
  Simulation lockstep = build_cruise(true, 10);
  for (std::uint64_t k = 0; k < 120; ++k) {
    ASSERT_FALSE(plain.step());
    ASSERT_FALSE(lockstep.step());
    for (const auto& [field, unit] : kExpectedFields) {
      EXPECT_EQ(bits(derived(plain, field)), bits(derived(lockstep, field)))
          << "frame " << k << " field " << field;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Frame-start timing and the previous-frame rule.

TEST(DerivedSignals, DescribeTheFrameStartStateAndVNIsTheRotatedBodyVelocity) {
  Simulation sim = build_cruise(false);
  for (std::uint64_t k = 0; k < 20; ++k) {
    const astra::StateVector start = sim.vehicles()[0].state();
    const astra::VehicleState vs{start};
    ASSERT_FALSE(sim.step());
    const Eigen::Vector3d v_n = astra::frames::rotate_b_to_n(vs.q_nb(), vs.v_b());
    EXPECT_EQ(bits(derived(sim, "v_n_0")), bits(v_n(0)));
    EXPECT_EQ(bits(derived(sim, "v_n_1")), bits(v_n(1)));
    EXPECT_EQ(bits(derived(sim, "v_n_2")), bits(v_n(2)));
    EXPECT_EQ(bits(derived(sim, "vertical_speed")), bits(-v_n(2)));
    EXPECT_EQ(bits(derived(sim, "u")), bits(vs.v_b()(0)));
    EXPECT_EQ(bits(derived(sim, "p")), bits(vs.omega_b()(0)));
    EXPECT_EQ(bits(derived(sim, "lat")), bits(vs.p_n()(0)));
  }
}

TEST(DerivedSignals, AccelIsInvalidAtFrameZeroThenThePreviousFrameWrenchOverMass) {
  Simulation sim = build_cruise(false);
  ASSERT_FALSE(sim.step());
  EXPECT_EQ(status(sim) & ASTRA_ICD_STATUS_ACCEL_INVALID, ASTRA_ICD_STATUS_ACCEL_INVALID);
  EXPECT_EQ(bits(derived(sim, "ax")), bits(0.0));
  EXPECT_EQ(bits(derived(sim, "ay")), bits(0.0));
  EXPECT_EQ(bits(derived(sim, "az")), bits(0.0));

  for (std::uint64_t k = 1; k < 10; ++k) {
    // Frame k-1's k1-stage wrench_total and mass, read before frame k runs.
    const std::array<double, 3> force = {*sim.registry().find("v0.wrench_total.force_b_x"),
                                         *sim.registry().find("v0.wrench_total.force_b_y"),
                                         *sim.registry().find("v0.wrench_total.force_b_z")};
    const double mass = sim.vehicles()[0].latched_mass();
    ASSERT_FALSE(sim.step());
    EXPECT_EQ(status(sim) & ASTRA_ICD_STATUS_ACCEL_INVALID, 0U) << "frame " << k;
    EXPECT_EQ(bits(derived(sim, "ax")), bits(force[0] / mass)) << "frame " << k;
    EXPECT_EQ(bits(derived(sim, "ay")), bits(force[1] / mass)) << "frame " << k;
    EXPECT_EQ(bits(derived(sim, "az")), bits(force[2] / mass)) << "frame " << k;
  }
}

// Critique F-6's case: derivative() latches a mass without integrating, so at frame 1 with no
// completed frame 0 the accelerations stay invalid. The flat fixture: a probe of a wind-configured
// vehicle is refused before its first step().
TEST(DerivedSignals, AProbeLatchedMassDoesNotMakeAccelValid) {
  Simulation sim = build_flat(false);
  const std::span<const double> y = sim.vehicles()[0].combined_state();
  const std::vector<double> state(y.begin(), y.end());
  static_cast<void>(sim.derivative(state));
  ASSERT_GT(sim.vehicles()[0].latched_mass(), 0.0);
  sim.preset_frame_for_testing(1);
  ASSERT_FALSE(sim.step());
  EXPECT_EQ(status(sim) & ASTRA_ICD_STATUS_ACCEL_INVALID, ASTRA_ICD_STATUS_ACCEL_INVALID);
  EXPECT_EQ(bits(derived(sim, "ax")), bits(0.0));
}

TEST(DerivedSignals, AccelIsInvalidAfterAFrameJumpAndValidAgainOneFrameLater) {
  Simulation sim = build_cruise(false);
  ASSERT_FALSE(sim.run(3));
  sim.preset_frame_for_testing(10);  // frame 9 was never completed here
  ASSERT_FALSE(sim.step());
  EXPECT_EQ(status(sim) & ASTRA_ICD_STATUS_ACCEL_INVALID, ASTRA_ICD_STATUS_ACCEL_INVALID);
  ASSERT_FALSE(sim.step());  // frame 11: frame 10 was completed
  EXPECT_EQ(status(sim) & ASTRA_ICD_STATUS_ACCEL_INVALID, 0U);
}

// Codex review F1: without a transport a halted frame is retried, and a halt at k2 comes after k1
// already overwrote the live wrench_total with frame k's wrench. The retry's ax must still be frame
// k-1's k1 force over mass. nesc_aero's drag changes every frame, so the two wrenches differ.
TEST(DerivedSignals, ARetriedFrameAfterAMidIntegrationHaltKeepsThePreviousFrameWrench) {
  Simulation sim = Simulation::from_config(
      Config::from_string(
          "sim:\n  dt: 0.001\n  frames: 100\n"
          "vehicle:\n  models:\n"
          "    - { plugin: tests/plugins/nesc_aero, s: 0.1, cd: 0.1 }\n"
          "    - { plugin: tests/plugins/nan_wrench, nan_frame: 5, nan_after_calls: 1 }\n"
          "physical:\n"
          "  earth: plugins/physical/earth/flat\n"
          "  gravity: plugins/physical/gravity/constant\n"
          "  atmosphere: plugins/physical/atmosphere/isa1976\n"
          "  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 1.0, "
          "izz: 1.0 }\n"
          "initial:\n  state: { p_n: [0.0, 0.0, -100.0], v_b: [60.0, 0.0, 0.0] }\n",
          "k2 halt"),
      ASTRA_TEST_PLUGIN_DIR);
  ASSERT_FALSE(sim.run(5));  // frames 0..4 complete
  const double* force_x = sim.registry().find("v0.wrench_total.force_b_x");
  ASSERT_NE(force_x, nullptr);
  const double frame_4_ax = *force_x / sim.vehicles()[0].latched_mass();

  for (int attempt = 0; attempt < 2; ++attempt) {
    const astra::HaltReason halt = sim.step();
    ASSERT_TRUE(halt) << "frame 5 halts at its k2 stage, attempt " << attempt;
    ASSERT_EQ(halt.frame, 5U);
    EXPECT_EQ(status(sim) & ASTRA_ICD_STATUS_ACCEL_INVALID, 0U) << "attempt " << attempt;
    EXPECT_EQ(bits(derived(sim, "ax")), bits(frame_4_ax)) << "attempt " << attempt;
  }
  // The live signal now holds the failed attempt's frame-5 k1 wrench, which differs: the check
  // above would fail if pack() read it.
  EXPECT_NE(bits(*force_x / sim.vehicles()[0].latched_mass()), bits(frame_4_ax));
}

// ---------------------------------------------------------------------------------------------
// ADR-0011: one set per physics vehicle, each from its own state.

TEST(DerivedSignals, EachPhysicsVehiclePublishesItsOwnSet) {
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin earth_loaded = loader.load("plugins/physical/earth/flat");
  auto* const earth = dynamic_cast<astra::EarthModel*>(earth_loaded.model.get());
  astra::SignalRegistry earth_registry;
  earth->init(Config::from_string("{}\n", "earth-flat-fixture"), earth_registry);
  astra::LoadedPlugin gravity_loaded = loader.load("plugins/physical/gravity/constant");
  auto* const gravity = dynamic_cast<astra::GravityModel*>(gravity_loaded.model.get());
  astra::SignalRegistry gravity_registry;
  gravity->init(Config::from_string("{ g: 0.0 }\n", "gravity-constant-fixture"), gravity_registry);
  const astra::Vehicle::ModelSpec mass_spec{
      "plugins/physical/mass/constant",
      Config::from_string("{ mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0 }\n", "mass-fixture"), 1};

  std::array<astra::StateVector, 2> initial_states{astra::StateVector::Zero(),
                                                   astra::StateVector::Zero()};
  for (std::size_t i = 0; i < initial_states.size(); ++i) {
    initial_states[i](astra::state::kQuaternion) = 1.0;
    initial_states[i](astra::state::kPosition + 2) = -100.0 * static_cast<double>(i + 1);
  }
  Simulation sim =
      Simulation::for_testing(loader, initial_states, {}, nullptr, earth, gravity, &mass_spec);
  ASSERT_FALSE(sim.step());
  EXPECT_EQ(bits(derived(sim, "altitude", "v0")), bits(100.0));
  EXPECT_EQ(bits(derived(sim, "altitude", "v1")), bits(200.0));
}

}  // namespace
