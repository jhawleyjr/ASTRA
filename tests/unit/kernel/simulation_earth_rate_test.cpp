// ADR-0032 (M7 PR 3b, docs/plans/m7-pr3b-earth-relative-rate.md, tests B1 to B6): the core sets
// EnvironmentSample::omega_ie_b = c_nb * earth.omega_ie_n(vs_stage) in every RK4 stage's
// environment, before every wrench_b() and integrate_state(), on the step path and on
// Simulation::derivative's probe path. tests/plugins/earth_rate_recorder records what each call
// was handed, slot k being the k-th call since the last step().
//
// The expected values are computed here, independently of the core:
// - Omega is typed from WGS-84, not read from geodesy::kOmegaIe.
// - omega_ie_n = Omega * (cos lat, 0, -sin lat).
// - C_nb comes either from a stated 3-2-1 Euler triple (S&L eq. 1.3-10, typed) or from a recorded
//   stage quaternion (homogeneous form, below).
//
// The tolerance, 1e-13 * Omega per component, is about 1000 times the expected roundoff (about
// 1e-16 * Omega). A frame or sign error is O(Omega), and reading the frame-start state instead of
// the stage's own is about 1e-4 * Omega here. A miss is a finding, never a reason to widen it.
#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/util/frames.hpp"

namespace {

using astra::Config;
using astra::HealthCheck;
using astra::Simulation;

// WGS-84's Earth rotation rate, rad/s (NIMA TR8350.2, Table 3.1).
constexpr double kOmegaWgs84 = 7.292115e-5;
constexpr double kTolerance = 1e-13 * kOmegaWgs84;
constexpr int kSlots = 4;
constexpr std::uint64_t kPositiveZeroBits = 0x0000000000000000ULL;

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

Eigen::Vector3d omega_ie_n(double lat) {
  return kOmegaWgs84 * Eigen::Vector3d{std::cos(lat), 0.0, -std::sin(lat)};
}

// S&L eq. 1.3-10: C_nb (v_b = C_nb v_n) for the 3-2-1 sequence psi, theta, phi, typed here.
Eigen::Matrix3d c_nb_from_euler(double psi, double theta, double phi) {
  const double cps = std::cos(psi);
  const double sps = std::sin(psi);
  const double cth = std::cos(theta);
  const double sth = std::sin(theta);
  const double cph = std::cos(phi);
  const double sph = std::sin(phi);
  Eigen::Matrix3d c;
  // clang-format off
  c << cth * cps,                      cth * sps,                     -sth,
       -cph * sps + sph * sth * cps,   cph * cps + sph * sth * sps,   sph * cth,
       sph * sps + cph * sth * cps,    -sph * cps + cph * sth * sps,  cph * cth;
  // clang-format on
  return c;
}

// C_nb from a recorded stage quaternion (w, x, y, z), in the homogeneous form, diagonal
// w^2 + x^2 - y^2 - z^2. That is what frames::dcm_from_quaternion evaluates, whatever source is
// cited. RK4 stage quaternions are not unit length. With A = Omega(omega)/2 skew-symmetric and
// q' = A q, the stages are q0 + (dt/2) A q0, then q0 + (dt/2) A q2, then q0 + dt A q3, so
// |q|^2 - 1 is:
// - stage 2: +(dt/2)^2 |omega|^2 / 4;
// - stage 3: -(dt/2)^2 |omega|^2 / 4;
// - stage 4: O(dt^4), unit to roundoff here.
// At the rotating fixture's rates that is +3.28e-9 and -3.28e-9 (measured: +3.283e-9 and
// -3.283e-9) at stages 2 and 3, and -1.1e-16 at stage 4. A form that assumes |q| = 1 (diagonal
// 1 - 2(y^2 + z^2)) would miss by about 3e-9 * Omega at stages 2 and 3, far outside the
// tolerance. That is a property of the form, never a reason to change the tolerance.
Eigen::Matrix3d c_nb_from_quaternion(const Eigen::Vector4d& q) {
  const double w = q(0);
  const double x = q(1);
  const double y = q(2);
  const double z = q(3);
  Eigen::Matrix3d c;
  // clang-format off
  c << w * w + x * x - y * y - z * z, 2.0 * (x * y + w * z),          2.0 * (x * z - w * y),
       2.0 * (x * y - w * z),          w * w - x * x + y * y - z * z, 2.0 * (y * z + w * x),
       2.0 * (x * z + w * y),          2.0 * (y * z - w * x),          w * w - x * x - y * y + z * z;
  // clang-format on
  return c;
}

Simulation load(const std::string& yaml, std::string_view origin) {
  return Simulation::from_config(Config::from_string(yaml, origin), ASTRA_TEST_PLUGIN_DIR);
}

// One recorded slot of one kind ("wrench" or "state").
struct Slot {
  Eigen::Vector3d omega_ie_b;
  Eigen::Vector3d omega_eb_b;
  Eigen::Vector4d q_nb;
  Eigen::Vector3d p_n;
  double altitude_m = 0.0;
};

double signal(const Simulation& sim, const std::string& name) {
  const double* value = sim.registry().find("v0.earth_rate_recorder." + name);
  EXPECT_NE(value, nullptr) << name;
  return value == nullptr ? std::nan("") : *value;
}

Slot slot(const Simulation& sim, std::string_view kind, int k) {
  const std::string base = std::string{kind} + "." + std::to_string(k) + ".";
  Slot s;
  for (int i = 0; i < 3; ++i) {
    s.omega_ie_b(i) = signal(sim, base + "omega_ie_b_" + std::to_string(i));
    s.omega_eb_b(i) = signal(sim, base + "omega_eb_b_" + std::to_string(i));
    s.p_n(i) = signal(sim, base + "p_n_" + std::to_string(i));
  }
  for (int i = 0; i < 4; ++i) {
    s.q_nb(i) = signal(sim, base + "q_nb_" + std::to_string(i));
  }
  s.altitude_m = signal(sim, base + "altitude_m");
  return s;
}

double calls(const Simulation& sim, std::string_view kind) {
  return signal(sim, std::string{kind} + "_calls");
}

void expect_near(const Eigen::Vector3d& actual, const Eigen::Vector3d& expected, const char* what) {
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(actual(i), expected(i), kTolerance) << what << " component " << i;
  }
}

void expect_positive_zero(const Eigen::Vector3d& value, const char* what) {
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(bits(value(i)), kPositiveZeroBits) << what << " component " << i << " is " << value(i)
                                                 << " (bits " << std::hex << bits(value(i)) << ")";
  }
}

std::string physical(std::string_view earth, std::string_view gravity) {
  return "physical:\n  earth: " + std::string{earth} + "\n  gravity: " + std::string{gravity} +
         "\n  mass: { plugin: plugins/physical/mass/constant, mass: 10.0, ixx: 1.0, iyy: 2.0, "
         "izz: 3.0 }\n";
}

constexpr std::string_view kRecorder =
    "vehicle:\n"
    "  models:\n"
    "    - { plugin: tests/plugins/earth_rate_recorder }\n";

// The rotating fixture (B1): wgs84_rotating with wgs84_j2, a pair PR 3's consistency check
// accepts.
std::string rotating_yaml() {
  return "sim: { dt: 0.001, frames: 1 }\n" + std::string{kRecorder} +
         physical("plugins/physical/earth/wgs84_rotating", "plugins/physical/gravity/wgs84_j2") +
         "initial:\n"
         "  state: { p_n: [0.6, -2.0, 1000.0], v_b: [120.0, 5.0, -3.0], euler_nb: [2.2, 0.3, "
         "-0.4], "
         "omega_b: [0.1, -0.05, 0.2] }\n";
}

// A full combined state (13 rigid-body doubles plus the recorder's one) for a probe.
std::vector<double> probe_state(const Eigen::Vector3d& p_n, const Eigen::Vector3d& v_b, double psi,
                                double theta, double phi, const Eigen::Vector3d& omega_b) {
  const astra::frames::Quaternion q =
      astra::frames::quaternion_from_euler(astra::frames::EulerAngles{psi, theta, phi});
  return {p_n(0), p_n(1), p_n(2), v_b(0),     v_b(1),     v_b(2),     q(0),
          q(1),   q(2),   q(3),   omega_b(0), omega_b(1), omega_b(2), 0.0};
}

// B2 and B3's stated attitude and position, different from rotating_yaml()'s initial state, so a
// field computed from the frame-start (combined) state cannot pass them.
constexpr double kProbePsi = 2.0;
constexpr double kProbeTheta = -0.2;
constexpr double kProbePhi = 0.5;
Eigen::Vector3d probe_position() { return {0.62, -1.9, 1500.0}; }

// B1: one real frame. Every stage's field matches c_nb * omega_ie_n at that stage's own recorded
// state, and wrench_b() and integrate_state() of one stage are handed the same bits.
TEST(SimulationEarthRate, EveryStepStageCarriesCnbTimesOmegaIeNOnWgs84Rotating) {
  Simulation sim = load(rotating_yaml(), "earth-rate-step-wgs84");
  ASSERT_FALSE(sim.step());

  ASSERT_EQ(calls(sim, "wrench"), 4.0);
  ASSERT_EQ(calls(sim, "state"), 4.0);
  for (int k = 0; k < kSlots; ++k) {
    SCOPED_TRACE("stage " + std::to_string(k + 1));
    const Slot wrench = slot(sim, "wrench", k);
    const Slot state = slot(sim, "state", k);
    const Eigen::Vector3d expected = c_nb_from_quaternion(wrench.q_nb) * omega_ie_n(wrench.p_n(0));
    expect_near(wrench.omega_ie_b, expected, "wrench_b's omega_ie_b");
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(bits(state.omega_ie_b(i)), bits(wrench.omega_ie_b(i)))
          << "integrate_state and wrench_b of one stage differ, component " << i;
    }
    for (int i = 0; i < 4; ++i) {
      EXPECT_EQ(bits(state.q_nb(i)), bits(wrench.q_nb(i))) << "stage state differs, q " << i;
    }
  }

  // The field is the stage's own, not the frame's: stage 2's differs from stage 1's.
  const Slot first = slot(sim, "wrench", 0);
  const Slot second = slot(sim, "wrench", 1);
  EXPECT_FALSE(bits(first.omega_ie_b(0)) == bits(second.omega_ie_b(0)) &&
               bits(first.omega_ie_b(1)) == bits(second.omega_ie_b(1)) &&
               bits(first.omega_ie_b(2)) == bits(second.omega_ie_b(2)));
  // This fixture does exercise non-unit stage quaternions (c_nb_from_quaternion's comment):
  // stages 2 and 3 are off unit length by about +/-3.3e-9, and stage 4 is unit to roundoff.
  EXPECT_GT(slot(sim, "wrench", 1).q_nb.squaredNorm() - 1.0, 1e-9);
  EXPECT_LT(slot(sim, "wrench", 2).q_nb.squaredNorm() - 1.0, -1e-9);
  EXPECT_LT(std::abs(slot(sim, "wrench", 3).q_nb.squaredNorm() - 1.0), 1e-12);

  // The latched sample keeps the NaN default: only the stage copies carry the field.
  const Eigen::Vector3d latched = sim.vehicles().front().environment().omega_ie_b;
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(std::isnan(latched(i))) << "latched omega_ie_b component " << i;
  }
}

// B2: Simulation::derivative's probe path sets the field at the probed state, not at the sim's own.
TEST(SimulationEarthRate, TheDerivativeProbeCarriesCnbTimesOmegaIeNOnWgs84Rotating) {
  Simulation sim = load(rotating_yaml(), "earth-rate-probe-wgs84");
  const std::vector<double> state =
      probe_state(probe_position(), Eigen::Vector3d{100.0, -4.0, 6.0}, kProbePsi, kProbeTheta,
                  kProbePhi, Eigen::Vector3d{0.05, 0.08, -0.1});
  static_cast<void>(sim.derivative(state));

  ASSERT_EQ(calls(sim, "wrench"), 1.0);
  ASSERT_EQ(calls(sim, "state"), 1.0);
  const Eigen::Vector3d expected =
      c_nb_from_euler(kProbePsi, kProbeTheta, kProbePhi) * omega_ie_n(probe_position()(0));
  expect_near(slot(sim, "wrench", 0).omega_ie_b, expected, "wrench_b's omega_ie_b");
  expect_near(slot(sim, "state", 0).omega_ie_b, expected, "integrate_state's omega_ie_b");
}

// B3: the sign and frame, physically. A vehicle at rest relative to the Earth turns with it, so
// its inertial rate is the Earth's and its Earth-relative rate is zero. Adding a known rate
// relative to the Earth must give that rate back, which an accessor with its operands swapped
// would return negated.
TEST(SimulationEarthRate, EarthRelativeRateIsTheRateRelativeToTheEarthInBodyAxes) {
  const Eigen::Vector3d earth_rate_b =
      c_nb_from_euler(kProbePsi, kProbeTheta, kProbePhi) * omega_ie_n(probe_position()(0));
  const Eigen::Vector3d omega_rel{3e-5, -5e-5, 2e-5};

  {
    SCOPED_TRACE("(i) at rest relative to the Earth");
    Simulation sim = load(rotating_yaml(), "earth-rate-at-rest");
    static_cast<void>(sim.derivative(probe_state(probe_position(), Eigen::Vector3d::Zero(),
                                                 kProbePsi, kProbeTheta, kProbePhi, earth_rate_b)));
    const Slot s = slot(sim, "wrench", 0);
    expect_near(s.omega_eb_b, Eigen::Vector3d::Zero(), "omega_eb_b at rest");
    expect_near(s.omega_ie_b, earth_rate_b, "omega_ie_b at rest");
  }
  {
    SCOPED_TRACE("(ii) rotating relative to the Earth");
    Simulation sim = load(rotating_yaml(), "earth-rate-relative");
    static_cast<void>(
        sim.derivative(probe_state(probe_position(), Eigen::Vector3d::Zero(), kProbePsi,
                                   kProbeTheta, kProbePhi, earth_rate_b + omega_rel)));
    expect_near(slot(sim, "wrench", 0).omega_eb_b, omega_rel, "omega_eb_b relative");
  }
}

// B4 and B5's attitude: C_nb's first row, (cos theta cos psi, cos theta sin psi, -sin theta), is
// all negative, so c_nb * 0 alone gives -0.0 in component x. The field must still be +0.0 bit for
// bit.
constexpr double kNegativeRowPsi = -2.5;
constexpr double kNegativeRowTheta = 0.3;
constexpr double kNegativeRowPhi = 0.0;

void expect_positive_zero_on_both_paths(const std::string& yaml, const Eigen::Vector3d& p_n,
                                        std::string_view origin) {
  const Eigen::Matrix3d c = c_nb_from_euler(kNegativeRowPsi, kNegativeRowTheta, kNegativeRowPhi);
  ASSERT_TRUE((c.row(0).array() < 0.0).all()) << "the fixture must have an all-negative C_nb row";

  Simulation stepped = load(yaml, origin);
  ASSERT_FALSE(stepped.step());
  ASSERT_EQ(calls(stepped, "wrench"), 4.0);
  ASSERT_EQ(calls(stepped, "state"), 4.0);
  for (int k = 0; k < kSlots; ++k) {
    SCOPED_TRACE("step path, stage " + std::to_string(k + 1));
    expect_positive_zero(slot(stepped, "wrench", k).omega_ie_b, "wrench_b's omega_ie_b");
    expect_positive_zero(slot(stepped, "state", k).omega_ie_b, "integrate_state's omega_ie_b");
  }

  Simulation probed = load(yaml, origin);
  static_cast<void>(probed.derivative(
      probe_state(p_n, Eigen::Vector3d{80.0, 1.0, 2.0}, kNegativeRowPsi, kNegativeRowTheta,
                  kNegativeRowPhi, Eigen::Vector3d{0.02, 0.01, -0.03})));
  SCOPED_TRACE("probe path");
  expect_positive_zero(slot(probed, "wrench", 0).omega_ie_b, "wrench_b's omega_ie_b");
  expect_positive_zero(slot(probed, "state", 0).omega_ie_b, "integrate_state's omega_ie_b");
}

std::string negative_row_initial(std::string_view p_n) {
  return "initial:\n  state: { p_n: " + std::string{p_n} +
         ", v_b: [80.0, 1.0, 2.0], euler_nb: [-2.5, 0.3, 0.0], omega_b: [0.02, 0.01, -0.03] }\n";
}

// B4: flat Earth.
TEST(SimulationEarthRate, IsPositiveZeroOnAFlatEarthOnBothPaths) {
  const std::string yaml =
      "sim: { dt: 0.001, frames: 1 }\n" + std::string{kRecorder} +
      physical("plugins/physical/earth/flat", "plugins/physical/gravity/constant") +
      negative_row_initial("[0.0, 0.0, -1000.0]");
  expect_positive_zero_on_both_paths(yaml, Eigen::Vector3d{0.0, 0.0, -1000.0},
                                     "earth-rate-flat-zero");
}

// B5: a round Earth that does not rotate (round's own default), with inverse-square gravity.
TEST(SimulationEarthRate, IsPositiveZeroOnANonRotatingRoundEarthOnBothPaths) {
  const std::string yaml =
      "sim: { dt: 0.001, frames: 1 }\n" + std::string{kRecorder} +
      physical("plugins/physical/earth/round", "plugins/physical/gravity/inverse_square") +
      negative_row_initial("[0.6, -2.0, 1000.0]");
  expect_positive_zero_on_both_paths(yaml, Eigen::Vector3d{0.6, -2.0, 1000.0},
                                     "earth-rate-round-zero");
}

std::string skipped_stage_yaml(std::string_view sim_extra, std::string_view atmosphere,
                               std::string_view initial_state) {
  return "sim: { dt: 0.001, frames: 1" + std::string{sim_extra} + " }\n" + std::string{kRecorder} +
         physical("plugins/physical/earth/flat", "plugins/physical/gravity/constant") +
         "  atmosphere: " + std::string{atmosphere} + "\n" +
         "initial:\n  state: " + std::string{initial_state} + "\n";
}

// B6: the field is set before both early returns of prepare_stage_environment, so a stage whose
// wrenches are skipped still hands integrate_state() a set value. Identity attitude: c_nb * 0 is
// +0.0 in every component there with or without the +0.0 canonicalization, so these cases test the
// assignment's place, not its zeros. The +0.0 assertions are bitwise.
TEST(SimulationEarthRate, IntegrateStateSeesItOnStagesWhoseWrenchesAreSkipped) {
  {
    SCOPED_TRACE("(i) the non-finite return: tests/plugins/nan_atmosphere");
    Simulation sim = load(skipped_stage_yaml("", "tests/plugins/nan_atmosphere",
                                             "{ p_n: [0.0, 0.0, -1000.0], v_b: [60.0, 0.0, 0.0] }"),
                          "earth-rate-skip-non-finite");
    const astra::HaltReason halt = sim.step();
    ASSERT_TRUE(halt);
    EXPECT_EQ(halt.check, HealthCheck::kNonFiniteEnvironment);
    EXPECT_EQ(halt.source, "tests/plugins/nan_atmosphere");
    EXPECT_EQ(calls(sim, "wrench"), 0.0);
    for (int k = 0; k < kSlots; ++k) {
      EXPECT_TRUE(slot(sim, "wrench", k).omega_ie_b.array().isNaN().all()) << "slot " << k;
    }
    ASSERT_EQ(calls(sim, "state"), 4.0);
    for (int k = 0; k < kSlots; ++k) {
      SCOPED_TRACE("stage " + std::to_string(k + 1));
      expect_positive_zero(slot(sim, "state", k).omega_ie_b, "integrate_state's omega_ie_b");
    }
  }
  {
    // isa1976's valid_range() ends at 86000 m. Start 0.01 m below it, climbing at 100 m/s: stage 1
    // (85999.99 m) is in range, stages 2 and 3 (about 86000.04 m, + 100 m/s * dt/2) and stage 4
    // (about 86000.09 m) are out, so the domain pre-check skips stages 2 to 4.
    SCOPED_TRACE("(ii) the out-of-range return: isa1976's upper limit crossed inside one frame");
    Simulation sim =
        load(skipped_stage_yaml(", health: { domain: halt }", "plugins/physical/atmosphere/isa1976",
                                "{ p_n: [0.0, 0.0, -85999.99], v_b: [0.0, 0.0, -100.0] }"),
             "earth-rate-skip-out-of-range");
    const astra::HaltReason halt = sim.step();
    ASSERT_TRUE(halt);
    EXPECT_EQ(halt.check, HealthCheck::kOutOfDomain);
    EXPECT_EQ(halt.source, "plugins/physical/atmosphere/isa1976");

    ASSERT_EQ(calls(sim, "state"), 4.0);
    // The predicted altitudes: stage 1 in range, stages 2 to 4 out.
    EXPECT_LE(-slot(sim, "state", 0).p_n(2), 86000.0);
    for (int k = 1; k < kSlots; ++k) {
      EXPECT_GT(-slot(sim, "state", k).p_n(2), 86000.0) << "stage " << k + 1;
    }
    EXPECT_EQ(calls(sim, "wrench"), 1.0);
    expect_positive_zero(slot(sim, "wrench", 0).omega_ie_b, "stage 1 wrench_b's omega_ie_b");
    for (int k = 1; k < kSlots; ++k) {
      EXPECT_TRUE(slot(sim, "wrench", k).omega_ie_b.array().isNaN().all()) << "slot " << k;
    }
    for (int k = 0; k < kSlots; ++k) {
      SCOPED_TRACE("stage " + std::to_string(k + 1));
      expect_positive_zero(slot(sim, "state", k).omega_ie_b, "integrate_state's omega_ie_b");
    }
  }
}

// ---------------------------------------------------------------------------------------------
// ADR-0033 (M7 PR 3c, docs/plans/m7-pr3c-altitude-and-units.md, tests B.1 to B.5): the core sets
// EnvironmentSample::altitude_m = EarthModel::altitude(vs_stage) in every RK4 stage's environment,
// first, with omega_ie_b, on the step and probe paths. The expected value is computed here from
// each slot's own recorded stage p_n, not by the core: p_n(2) under wgs84_rotating (already the
// height above the ellipsoid, ADR-0003) and -p_n(2) under earth/flat. Every comparison is bitwise.

void expect_same_bits(double actual, double expected, const char* what) {
  EXPECT_EQ(bits(actual), bits(expected))
      << what << " is " << actual << ", expected " << expected << " (bits " << std::hex
      << bits(actual) << " vs " << bits(expected) << ")";
}

// B.1: no atmosphere is configured, so before M7 PR 3c no stage computed an altitude at all.
TEST(SimulationStageAltitude, EveryStepStageCarriesTheEarthModelsAltitudeOnWgs84Rotating) {
  Simulation sim = load(rotating_yaml(), "stage-altitude-step-wgs84");
  ASSERT_FALSE(sim.step());

  ASSERT_EQ(calls(sim, "wrench"), 4.0);
  ASSERT_EQ(calls(sim, "state"), 4.0);
  for (int k = 0; k < kSlots; ++k) {
    SCOPED_TRACE("stage " + std::to_string(k + 1));
    const Slot wrench = slot(sim, "wrench", k);
    const Slot state = slot(sim, "state", k);
    expect_same_bits(wrench.altitude_m, wrench.p_n(2), "wrench_b's altitude_m");
    expect_same_bits(state.altitude_m, wrench.altitude_m, "integrate_state's altitude_m");
  }
  // The stage's own altitude, not the frame's.
  EXPECT_NE(bits(slot(sim, "wrench", 1).altitude_m), bits(slot(sim, "wrench", 0).altitude_m));
}

// B.2: the probe sets the field at the probed state (1500 m), not at the sim's own (1000 m).
TEST(SimulationStageAltitude, TheDerivativeProbeCarriesTheProbedStatesAltitudeOnWgs84Rotating) {
  Simulation sim = load(rotating_yaml(), "stage-altitude-probe-wgs84");
  static_cast<void>(
      sim.derivative(probe_state(probe_position(), Eigen::Vector3d{100.0, -4.0, 6.0}, kProbePsi,
                                 kProbeTheta, kProbePhi, Eigen::Vector3d{0.05, 0.08, -0.1})));

  ASSERT_EQ(calls(sim, "wrench"), 1.0);
  ASSERT_EQ(calls(sim, "state"), 1.0);
  expect_same_bits(slot(sim, "wrench", 0).altitude_m, 1500.0, "wrench_b's altitude_m");
  expect_same_bits(slot(sim, "state", 0).altitude_m, 1500.0, "integrate_state's altitude_m");
}

std::string flat_with_atmosphere_yaml() {
  return "sim: { dt: 0.001, frames: 1 }\n" + std::string{kRecorder} +
         physical("plugins/physical/earth/flat", "plugins/physical/gravity/constant") +
         "  atmosphere: plugins/physical/atmosphere/isa1976\n" +
         negative_row_initial("[0.0, 0.0, -1000.0]");
}

// B.3: earth/flat, with an in-range atmosphere sampled at the same value.
TEST(SimulationStageAltitude, IsMinusPnDownOnAFlatEarthOnBothPaths) {
  Simulation stepped = load(flat_with_atmosphere_yaml(), "stage-altitude-step-flat");
  ASSERT_FALSE(stepped.step());
  ASSERT_EQ(calls(stepped, "wrench"), 4.0);
  ASSERT_EQ(calls(stepped, "state"), 4.0);
  for (int k = 0; k < kSlots; ++k) {
    SCOPED_TRACE("step path, stage " + std::to_string(k + 1));
    const Slot wrench = slot(stepped, "wrench", k);
    expect_same_bits(wrench.altitude_m, -wrench.p_n(2), "wrench_b's altitude_m");
    expect_same_bits(slot(stepped, "state", k).altitude_m, wrench.altitude_m,
                     "integrate_state's altitude_m");
  }
  EXPECT_NE(bits(slot(stepped, "wrench", 1).altitude_m),
            bits(slot(stepped, "wrench", 0).altitude_m));

  Simulation probed = load(flat_with_atmosphere_yaml(), "stage-altitude-probe-flat");
  static_cast<void>(probed.derivative(probe_state(
      Eigen::Vector3d{0.0, 0.0, -1500.0}, Eigen::Vector3d{80.0, 1.0, 2.0}, kNegativeRowPsi,
      kNegativeRowTheta, kNegativeRowPhi, Eigen::Vector3d{0.02, 0.01, -0.03})));
  SCOPED_TRACE("probe path");
  expect_same_bits(slot(probed, "wrench", 0).altitude_m, 1500.0, "wrench_b's altitude_m");
  expect_same_bits(slot(probed, "state", 0).altitude_m, 1500.0, "integrate_state's altitude_m");
}

// B.4: B6's two fixtures. The field is set before both early returns of
// prepare_stage_environment, so a stage whose wrenches are skipped still hands integrate_state()
// its own altitude.
TEST(SimulationStageAltitude, IntegrateStateSeesItOnStagesWhoseWrenchesAreSkipped) {
  {
    SCOPED_TRACE("(i) the non-finite return: tests/plugins/nan_atmosphere");
    Simulation sim = load(skipped_stage_yaml("", "tests/plugins/nan_atmosphere",
                                             "{ p_n: [0.0, 0.0, -1000.0], v_b: [60.0, 0.0, 0.0] }"),
                          "stage-altitude-skip-non-finite");
    const astra::HaltReason halt = sim.step();
    ASSERT_TRUE(halt);
    EXPECT_EQ(halt.check, HealthCheck::kNonFiniteEnvironment);
    EXPECT_EQ(calls(sim, "wrench"), 0.0);
    for (int k = 0; k < kSlots; ++k) {
      EXPECT_TRUE(std::isnan(slot(sim, "wrench", k).altitude_m)) << "slot " << k;
    }
    ASSERT_EQ(calls(sim, "state"), 4.0);
    for (int k = 0; k < kSlots; ++k) {
      SCOPED_TRACE("stage " + std::to_string(k + 1));
      const Slot state = slot(sim, "state", k);
      expect_same_bits(state.altitude_m, -state.p_n(2), "integrate_state's altitude_m");
    }
  }
  {
    SCOPED_TRACE("(ii) the out-of-range return: isa1976's upper limit crossed inside one frame");
    Simulation sim =
        load(skipped_stage_yaml(", health: { domain: halt }", "plugins/physical/atmosphere/isa1976",
                                "{ p_n: [0.0, 0.0, -85999.99], v_b: [0.0, 0.0, -100.0] }"),
             "stage-altitude-skip-out-of-range");
    const astra::HaltReason halt = sim.step();
    ASSERT_TRUE(halt);
    EXPECT_EQ(halt.check, HealthCheck::kOutOfDomain);

    ASSERT_EQ(calls(sim, "wrench"), 1.0);
    const Slot first = slot(sim, "wrench", 0);
    expect_same_bits(first.altitude_m, -first.p_n(2), "stage 1 wrench_b's altitude_m");
    EXPECT_LE(first.altitude_m, 86000.0);
    ASSERT_EQ(calls(sim, "state"), 4.0);
    for (int k = 0; k < kSlots; ++k) {
      SCOPED_TRACE("stage " + std::to_string(k + 1));
      const Slot state = slot(sim, "state", k);
      expect_same_bits(state.altitude_m, -state.p_n(2), "integrate_state's altitude_m");
      if (k > 0) {
        EXPECT_GT(state.altitude_m, 86000.0);
      }
    }
  }
}

// B.5: the latched sample keeps the NaN default, with and without an atmosphere configured.
TEST(SimulationStageAltitude, TheLatchedSampleKeepsNaN) {
  Simulation without = load(rotating_yaml(), "stage-altitude-latched-wgs84");
  ASSERT_FALSE(without.step());
  EXPECT_TRUE(std::isnan(without.vehicles().front().environment().altitude_m));

  Simulation with = load(flat_with_atmosphere_yaml(), "stage-altitude-latched-flat");
  ASSERT_FALSE(with.step());
  EXPECT_TRUE(std::isnan(with.vehicles().front().environment().altitude_m));
}

}  // namespace
