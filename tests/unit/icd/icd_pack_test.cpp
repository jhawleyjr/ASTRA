// astra::icd_pack::pack_sim_to_sw (core/include/astra/util/icd_pack.hpp, M4 PR 2, R12a): every
// packed astra_sim_to_sw_v1 field against an oracle derived INDEPENDENTLY here -- plain arithmetic
// and libm on hand-chosen numbers, the human-owned ADR-0001 golden (tests/golden/frames/
// adr0001_cases.hpp, numpy) for attitude, never a call into frames::/air_data:: to build an
// expectation. A test that builds q with the same formula the code uses and checks the angles come
// back cannot catch a q_nb/q_bn swap; the golden's C_nb and the single-axis physical-sign cases
// below do.
//
// Textbook: Johnson/Lewis/Stevens, Aircraft Control and Simulation, 3rd ed. (2015) as shipped in
// docs/refs/textbooks: the DCM C_frd/ned is Eq. (1.3-10), Euler angles from the DCM Eq. (1.3-11),
// q_b/a for a right-handed coordinate rotation Eq. (1.8-10a), the half-angle yaw-pitch-roll
// quaternion Example 1.8-1 Eq. (1) (p. 52), q -> DCM array form Eq. (1.8-16) and its expansion
// Eq. (1.8-18).
#include "astra/util/icd_pack.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/interfaces/atmosphere_model.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/kernel/virtual_clock.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "golden/frames/adr0001_cases.hpp"
#include "icd/astra_icd.h"

namespace {

using astra::AtmoState;
using astra::EarthModel;
using astra::EnvironmentSample;
using astra::StateVector;
using astra::StepContext;
using astra::VehicleState;
using astra::icd_pack::pack_sim_to_sw;
using astra::icd_pack::PreviousFrame;

constexpr double kTol = 1e-11;
// Eigen's braced default is uninitialized; every "no wind / no rate" below is this explicit zero.
const Eigen::Vector3d kZero = Eigen::Vector3d::Zero();
constexpr double kDeg = std::numbers::pi / 180.0;
constexpr double kStandardGravity = 9.80665;
constexpr std::uint32_t kKnownFlags =
    ASTRA_ICD_STATUS_ACCEL_INVALID | ASTRA_ICD_STATUS_LATLON_NOT_MODELED |
    ASTRA_ICD_STATUS_ATMOSPHERE_INVALID | ASTRA_ICD_STATUS_CAS_SUPERSONIC |
    ASTRA_ICD_STATUS_WIND_INVALID;

// The five bit values are ABI: pinned as literals so a renumbering is a test failure.
static_assert(ASTRA_ICD_STATUS_ACCEL_INVALID == 1U);
static_assert(ASTRA_ICD_STATUS_LATLON_NOT_MODELED == 2U);
static_assert(ASTRA_ICD_STATUS_ATMOSPHERE_INVALID == 4U);
static_assert(ASTRA_ICD_STATUS_CAS_SUPERSONIC == 8U);
static_assert(ASTRA_ICD_STATUS_WIND_INVALID == 16U);

// A sea-level-ish standard atmosphere at 10 km (U.S. Standard Atmosphere 1976 table: p = 26436 Pa,
// a = 299.53 m/s); only pressure and speed of sound are read by the pack.
AtmoState atmosphere(double pressure_pa, double speed_of_sound) {
  AtmoState a;
  a.pressure_pa = pressure_pa;
  a.speed_of_sound_m_s = speed_of_sound;
  return a;
}

const astra::test::adr0001::Case& golden(std::string_view name) {
  for (const auto& c : astra::test::adr0001::kCases) {
    if (name == c.name) {
      return c;
    }
  }
  ADD_FAILURE() << "no ADR-0001 golden case " << name;
  return astra::test::adr0001::kCases[0];
}

StateVector make_state(const Eigen::Vector3d& p_n, const Eigen::Vector3d& v_b,
                       const Eigen::Vector4d& q_nb, const Eigen::Vector3d& omega_b) {
  StateVector y = StateVector::Zero();
  y.segment<astra::state::kPositionSize>(astra::state::kPosition) = p_n;
  y.segment<astra::state::kVelocitySize>(astra::state::kVelocity) = v_b;
  y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion) = q_nb;
  y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate) = omega_b;
  return y;
}

Eigen::Vector4d golden_q(const astra::test::adr0001::Case& c) {
  return {c.q_nb[0], c.q_nb[1], c.q_nb[2], c.q_nb[3]};
}

StateVector level_state(const Eigen::Vector3d& v_b) {
  return make_state(Eigen::Vector3d::Zero(), v_b, {1.0, 0.0, 0.0, 0.0}, Eigen::Vector3d::Zero());
}

StepContext context_at(std::uint64_t frame, double dt = 0.001) {
  astra::VirtualClock clock{dt};
  for (std::uint64_t i = 0; i < frame; ++i) {
    clock.advance();
  }
  return clock.context();
}

// Minimal Earth stubs for the cases that need no plugin: the pack reads only altitude() and
// position_representation() (the real plugins are exercised separately below).
class StubEarth final : public EarthModel {
 public:
  StubEarth(astra::PositionRepresentation representation, double altitude_sign)
      : representation_(representation), altitude_sign_(altitude_sign) {}
  void init([[maybe_unused]] const astra::Config& config,
            [[maybe_unused]] astra::SignalRegistry& registry) override {}
  void reset() override {}
  void step([[maybe_unused]] const StepContext& ctx) override {}
  [[nodiscard]] std::string_view name() const override { return "stub_earth"; }
  [[nodiscard]] Eigen::Vector3d p_n_dot(
      [[maybe_unused]] const VehicleState& vs) const noexcept override {
    return Eigen::Vector3d::Zero();
  }
  [[nodiscard]] Eigen::Vector3d omega_ie_n(
      [[maybe_unused]] const VehicleState& vs) const noexcept override {
    return Eigen::Vector3d::Zero();
  }
  [[nodiscard]] Eigen::Vector3d omega_en_n(
      [[maybe_unused]] const VehicleState& vs) const noexcept override {
    return Eigen::Vector3d::Zero();
  }
  [[nodiscard]] double altitude(const VehicleState& vs) const noexcept override {
    return altitude_sign_ * vs.p_n()(2);
  }
  [[nodiscard]] astra::PositionRepresentation position_representation() const noexcept override {
    return representation_;
  }

 private:
  astra::PositionRepresentation representation_;
  double altitude_sign_;
};

const StubEarth& flat_stub() {
  static const StubEarth kEarth{astra::PositionRepresentation::kFlatNed, -1.0};
  return kEarth;
}
const StubEarth& round_stub() {
  static const StubEarth kEarth{astra::PositionRepresentation::kGeodetic, 1.0};
  return kEarth;
}

EnvironmentSample env_with(const Eigen::Vector3d& wind_n, const AtmoState& atmo) {
  EnvironmentSample env;
  env.wind_n = wind_n;
  env.atmosphere = atmo;
  return env;
}

astra_sim_to_sw_v1 pack(const StepContext& ctx, const StateVector& y, const EnvironmentSample& env,
                        const EarthModel& earth, const PreviousFrame* previous = nullptr) {
  astra_sim_to_sw_v1 out;
  pack_sim_to_sw(out, ctx, VehicleState{y}, env, earth, previous);
  return out;
}

bool all_fields_finite(const astra_sim_to_sw_v1& o) {
  const std::array<double, 22> fields{o.t,
                                      o.phi,
                                      o.theta,
                                      o.psi,
                                      o.p,
                                      o.q,
                                      o.r,
                                      o.u,
                                      o.v,
                                      o.w,
                                      o.alpha,
                                      o.beta,
                                      o.tas,
                                      o.cas,
                                      o.mach,
                                      o.altitude,
                                      o.vertical_speed,
                                      o.lat,
                                      o.lon,
                                      o.ax,
                                      o.ay,
                                      o.az};
  return std::ranges::all_of(fields, [](double x) { return std::isfinite(x); });
}

// ---- identity, time, zeroing ---------------------------------------------------------------

TEST(IcdPack, HeaderFrameAndTimeAreExactlyTheClocks) {
  // t must be bit-equal to VirtualClock::t() = static_cast<double>(frame) * dt (ADR-0010), which
  // R12b's 1e-12 check depends on. Expected here: 12345 * 0.001, computed in the test.
  const StepContext ctx = context_at(12345);
  const auto out = pack(ctx, level_state({100.0, 0.0, 0.0}),
                        env_with(kZero, atmosphere(1.0e5, 340.0)), flat_stub());
  EXPECT_EQ(out.magic, ASTRA_ICD_MAGIC);
  EXPECT_EQ(out.version, ASTRA_ICD_VERSION);
  EXPECT_EQ(out.frame, 12345U);
  EXPECT_EQ(out.t, static_cast<double>(12345) * 0.001);
}

TEST(IcdPack, EveryByteIsDefinedEvenFromGarbage) {
  // The struct has no padding (200 bytes = its fields), so the guard is not "padding is zero": it
  // is that a fully-filled 0xAB buffer comes out with reserved == 0 and no undefined status bit.
  astra_sim_to_sw_v1 out;
  std::memset(&out, 0xAB, sizeof out);
  const StateVector y = level_state({100.0, 0.0, 0.0});
  astra::icd_pack::pack_sim_to_sw(out, context_at(3), VehicleState{y},
                                  env_with(kZero, atmosphere(1.0e5, 340.0)), round_stub(), nullptr);
  EXPECT_EQ(out.reserved, 0U);
  EXPECT_EQ(out.status_flags & ~kKnownFlags, 0U);
  EXPECT_EQ(out.status_flags, ASTRA_ICD_STATUS_ACCEL_INVALID);  // nothing else in this scenario
  EXPECT_EQ(out.magic, ASTRA_ICD_MAGIC);
  EXPECT_TRUE(all_fields_finite(out));
}

// ---- attitude: pinned by the independent numpy golden, rotation direction included -------------

void expect_attitude_matches_golden(std::string_view name) {
  const auto& c = golden(name);
  SCOPED_TRACE(c.name);
  const StateVector y = make_state(Eigen::Vector3d::Zero(), Eigen::Vector3d::Zero(), golden_q(c),
                                   Eigen::Vector3d::Zero());
  const auto out = pack(context_at(1), y, env_with(kZero, atmosphere(1.0e5, 340.0)), flat_stub());
  // golden euler_nb is (psi, theta, phi); the ICD is (phi, theta, psi).
  EXPECT_NEAR(out.psi, c.euler_nb[0], astra::test::adr0001::kTolerance);
  EXPECT_NEAR(out.theta, c.euler_nb[1], astra::test::adr0001::kTolerance);
  EXPECT_NEAR(out.phi, c.euler_nb[2], astra::test::adr0001::kTolerance);
}

TEST(IcdPack, AttitudeCompositeMatchesGolden) {  // psi=30, theta=20, phi=10 deg
  expect_attitude_matches_golden("composite");
}

TEST(IcdPack, AttitudeCompositeNegMatchesGolden) {  // psi=-135, theta=-60, phi=170 deg
  expect_attitude_matches_golden("composite_neg");
}

TEST(IcdPack, RatesAndBodyVelocityPassThrough) {
  const StateVector y =
      make_state({1.0, 2.0, 3.0}, {101.0, -7.0, 5.5}, {1.0, 0.0, 0.0, 0.0}, {0.01, -0.02, 0.03});
  const auto out = pack(context_at(1), y, env_with(kZero, atmosphere(1.0e5, 340.0)), flat_stub());
  EXPECT_EQ(out.p, 0.01);
  EXPECT_EQ(out.q, -0.02);
  EXPECT_EQ(out.r, 0.03);
  EXPECT_EQ(out.u, 101.0);
  EXPECT_EQ(out.v, -7.0);
  EXPECT_EQ(out.w, 5.5);
}

// ---- vertical speed and air data: golden C_nb, then single-axis physical signs -----------------

TEST(IcdPack, VerticalSpeedIsMinusTheDownComponentOfVnFromGoldenDcm) {
  // v_n = C_nb^T v_b, so v_n(down) = sum_i C_nb[i][2] * v_b[i]; climb rate is its negative.
  const auto& c = golden("composite");
  const Eigen::Vector3d v_b{100.0, 10.0, -5.0};
  const auto out = pack(context_at(1), make_state(kZero, v_b, golden_q(c), kZero),
                        env_with(kZero, atmosphere(1.0e5, 340.0)), flat_stub());
  double v_down = 0.0;
  for (int i = 0; i < 3; ++i) {
    v_down += c.C_nb[i][2] * v_b(i);
  }
  EXPECT_NEAR(out.vertical_speed, -v_down, kTol);
}

TEST(IcdPack, PitchUpTenDegreesFlyingForwardClimbs) {
  // Physical sign: nose up 10 deg, velocity along the body x axis => climbing at V sin(10 deg).
  // q_nb for a pure pitch is (cos(theta/2), 0, sin(theta/2), 0) (S&L Eq. 1.8-10a, right-handed
  // rotation about y; the golden's pitch_30 is the same form).
  constexpr double kV = 100.0;
  const double half = 10.0 * kDeg / 2.0;
  const auto out =
      pack(context_at(1),
           make_state(kZero, {kV, 0.0, 0.0}, {std::cos(half), 0.0, std::sin(half), 0.0}, kZero),
           env_with(kZero, atmosphere(1.0e5, 340.0)), flat_stub());
  EXPECT_GT(out.vertical_speed, 0.0);
  EXPECT_NEAR(out.vertical_speed, kV * std::sin(10.0 * kDeg), kTol);  // 17.364817766693033
  EXPECT_NEAR(out.theta, 10.0 * kDeg, kTol);
}

TEST(IcdPack, AirDataCompositeAttitudeWithWindFromGoldenDcm) {
  // v_rel_b = v_b - C_nb wind_n, every step in plain arithmetic on the golden C_nb.
  const auto& c = golden("composite");
  const Eigen::Vector3d v_b{120.0, 8.0, 12.0};
  const Eigen::Vector3d wind_n{5.0, -3.0, 2.0};
  const auto out = pack(context_at(1), make_state(kZero, v_b, golden_q(c), kZero),
                        env_with(wind_n, atmosphere(26436.0, 299.53)), flat_stub());
  std::array<double, 3> rel{};
  for (int i = 0; i < 3; ++i) {
    double wind_b = 0.0;
    for (int j = 0; j < 3; ++j) {
      wind_b += c.C_nb[i][j] * wind_n(j);
    }
    rel.at(static_cast<std::size_t>(i)) = v_b(i) - wind_b;
  }
  const double tas = std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
  EXPECT_NEAR(out.tas, tas, kTol);
  EXPECT_NEAR(out.alpha, std::atan2(rel[2], rel[0]), kTol);
  EXPECT_NEAR(out.beta, std::asin(rel[1] / tas), kTol);  // S&L Eq. (2.3-6b)
  EXPECT_NEAR(out.mach, tas / 299.53, kTol);
}

TEST(IcdPack, StillAirPositiveWzGivesPositiveAlpha) {
  // Velocity has a downward body-z component (FRD z down): the relative wind comes up at the belly,
  // the nose is above the velocity vector => alpha = atan2(10, 100) > 0.
  const auto out = pack(context_at(1), level_state({100.0, 0.0, 10.0}),
                        env_with(kZero, atmosphere(1.0e5, 340.0)), flat_stub());
  EXPECT_NEAR(out.alpha, std::atan(0.1), kTol);  // 0.09966865249116204
  EXPECT_NEAR(out.beta, 0.0, kTol);
}

TEST(IcdPack, YawEastIntoANorthWindGivesPositiveBeta) {
  // Nose east (psi = 90 deg, the golden's yaw_90; north in body axes is (0,-1,0)), flying 100 m/s
  // forward, air moving north at 10 m/s ("wind-to", wind_n = (10,0,0)). The air comes from the
  // south, i.e. from the vehicle's right (right of east is south): v_rel_b = (100,0,0) - (0,-10,0)
  // = (100,10,0), beta = atan2(10, 100) > 0 (relative wind on the right side).
  const auto& c = golden("yaw_90");
  const auto out = pack(context_at(1), make_state(kZero, {100.0, 0.0, 0.0}, golden_q(c), kZero),
                        env_with({10.0, 0.0, 0.0}, atmosphere(1.0e5, 340.0)), flat_stub());
  EXPECT_GT(out.beta, 0.0);
  EXPECT_NEAR(out.beta, std::atan(0.1), kTol);
  EXPECT_NEAR(out.alpha, 0.0, kTol);
  EXPECT_NEAR(out.tas, std::sqrt(100.0 * 100.0 + 10.0 * 10.0), kTol);
  EXPECT_NEAR(out.psi, 90.0 * kDeg, kTol);
}

// ---- calibrated airspeed ---------------------------------------------------------------------

TEST(IcdPack, CasMatchesPublishedIsentropicTableAtTenKilometers) {
  // The discriminating anchor: a published-table inversion at ALTITUDE. (At p = p0 the forward and
  // inverse relations cancel, so cas = M a0 for any matched exponent pair; it would prove nothing.)
  // NACA Report 1135 (Ames, 1953) Table I "Subsonic flow" (gamma = 1.4) prints the static to total
  // pressure ratio p/pt to 4 decimals: M = 0.80 -> .6560, M = 0.43 -> .8807, M = 0.44 -> .8755.
  // Transcribed from the text layer of the NTRS scan (document 19930091059) and cross-checked
  // against (1 + 0.2 M^2)^-3.5, which agrees to every printed digit at all three Mach numbers.
  // ISA 10 km (U.S. Std Atmosphere 1976: p = 26436 Pa, a = 299.53 m/s), M = 0.8, so the table
  // gives pt/p = 1/0.6560 = 1.524390 and qc = (1/0.6560 - 1) * 26436 = 13862.78 Pa,
  // qc/p0 + 1 = 1.136815. The calibrated Mach Mc is the Mach whose tabulated pt/p equals that,
  // i.e. p/pt = 1/1.136815 = 0.879651; interpolating the printed p/pt column linearly between the
  // rows gives Mc = 0.43 + 0.01 * (0.8807 - 0.879651) / (0.8807 - 0.8755) = 0.432018, and
  // cas = Mc * a0, a0 = 340.29411 m/s (sqrt(1.4 * (8314.32/28.9644) * 288.15)): 147.013 m/s.
  // Tolerance, in Mach then times a0. With g = p/pt, |g'| >= 0.511 and |g''| <= 0.808 on
  // [0.43, 0.44]: (a) the rows' 4-decimal rounding, +/-5e-5 in g at either row, moves the
  // interpolated value by at most 5e-5, i.e. 5e-5 / 0.511 = 9.8e-5 in Mach; (b) the .6560
  // rounding (+/-5e-5 in p/pt at M = 0.8: pt/p +/-1.16e-4, qc/p0 +/-3.0e-5) moves the target g by
  // at most 2.35e-5, i.e. 4.6e-5 in Mach; (c) linear interpolation error <= (h^2 g''/8) / |g'| =
  // (0.01^2 * 0.808 / 8) / 0.511 = 2.0e-5 in Mach. Sum 1.63e-4 Mach * 340.29 = 0.056 m/s, so
  // 0.06 m/s. (The implementation's exact M = 0.8 gives 146.984, 0.029 m/s below the printed-digit
  // value, inside the budget. |g''| reaches about 0.8074 on the interval. The gap is mostly the
  // M 0.43/0.44 row rounding, about 0.026 m/s, with the .6560 rounding about 0.007 and the linear
  // interpolation about -0.004.) A wrong exponent pair (3.5 -> 3.4, or 2/7 -> 0.29) moves cas by
  // more than 1 m/s here, so this test discriminates. So does the "changed-gamma pair" mutant,
  // which means matched exponents 3.4 and 1/3.4 with the 0.2 and 5 factors held. A fully
  // consistent gamma change (1.41) moves cas by only 0.010 m/s, which this table test does not
  // catch and which is physically negligible.
  constexpr double kA0 = 340.29411;
  constexpr double kRowLowMach = 0.43;
  constexpr double kRowLowRatio = 0.8807;   // p/pt at M = 0.43
  constexpr double kRowHighRatio = 0.8755;  // p/pt at M = 0.44
  constexpr double kPrintedRatioAtMach08 = 0.6560;
  const double impact_over_sea_level =
      (1.0 / kPrintedRatioAtMach08 - 1.0) * 26436.0 / 101325.0;  // qc / p0
  const double target_ratio = 1.0 / (impact_over_sea_level + 1.0);
  const double calibrated_mach =
      kRowLowMach + 0.01 * (target_ratio - kRowLowRatio) / (kRowHighRatio - kRowLowRatio);
  const double tas = 0.8 * 299.53;
  const auto out = pack(context_at(1), level_state({tas, 0.0, 0.0}),
                        env_with(kZero, atmosphere(26436.0, 299.53)), flat_stub());
  EXPECT_NEAR(out.mach, 0.8, 1e-12);
  EXPECT_NEAR(out.cas, calibrated_mach * kA0, 0.06);
  EXPECT_EQ(
      out.status_flags & (ASTRA_ICD_STATUS_ATMOSPHERE_INVALID | ASTRA_ICD_STATUS_CAS_SUPERSONIC),
      0U);
}

TEST(IcdPack, CasAtTenKilometersHandComputed) {
  // Discriminates only implementation slips (a typo, a wrong constant): the expected value is this
  // same closed form evaluated in Python, so it cannot catch a wrong relation; the table anchor
  // above does that. ISA 10 km (U.S. Std Atmosphere 1976: p = 26436 Pa, a = 299.53 m/s), M = 0.8,
  // tas = 239.624 m/s. qc = p ((1 + 0.2 M^2)^3.5 - 1) = 26436 * 0.52434001 = 13861.4525 Pa, cas =
  // a0 sqrt(5 ((qc/p0 + 1)^(2/7) - 1)) = 146.98437733646873 m/s (evaluated in Python, independent
  // of the C++; ~285.7 kt, a typical M0.80 cruise CAS).
  const double tas = 0.8 * 299.53;
  const auto out = pack(context_at(1), level_state({tas, 0.0, 0.0}),
                        env_with(kZero, atmosphere(26436.0, 299.53)), flat_stub());
  EXPECT_NEAR(out.tas, 239.624, 1e-9);
  EXPECT_NEAR(out.mach, 0.8, 1e-12);
  EXPECT_NEAR(out.cas, 146.98437733646873, 1e-9);
}

// The real isa1976 plugin: sea-level identity and the low-Mach equivalent-airspeed anchor.
astra::AtmosphereModel& load_isa() {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded = loader.load("plugins/physical/atmosphere/isa1976");
  auto* const atmosphere_model = dynamic_cast<astra::AtmosphereModel*>(loaded.model.get());
  astra::SignalRegistry registry;
  atmosphere_model->init(astra::Config::from_string("{}\n", "isa1976-fixture"), registry);
  return *atmosphere_model;
}

TEST(IcdPack, CasEqualsTasAtIsaSeaLevelAgainstTheRealPlugin) {
  // At ISA sea level CAS == TAS by construction (same p0 and a0 in both). Uses the plugin's own
  // pressure and speed of sound so a constants mismatch would show: a 340.294 literal is 1.1e-4
  // m/s below a0 = sqrt(1.4 * R * 288.15) = 340.29411 m/s, R = 8314.32 / 28.9644 = 287.0531. This
  // discriminates only the sea-level constants (p0, a0) against the plugin; the exponents are
  // anchored by the table test above.
  const AtmoState sea_level = load_isa().at(0.0);
  for (const double tas : {50.0, 200.0, 300.0}) {
    SCOPED_TRACE(tas);
    const auto out =
        pack(context_at(1), level_state({tas, 0.0, 0.0}), env_with(kZero, sea_level), flat_stub());
    EXPECT_NEAR(out.cas, tas, 1e-9);
  }
}

TEST(IcdPack, LowMachCasApproachesEquivalentAirspeed) {
  // EAS = TAS sqrt(rho/rho0) (S&L Sec. 2.2 / any airspeed text). qc = q (1 + M^2/4 + ...), so
  // matching the same qc at altitude and at sea level gives CAS/EAS = sqrt((1 + M^2/4)/(1 +
  // Mc^2/4)) ~ 1 + (M^2 - Mc^2)/8 with 0 < Mc < M; hence |CAS/EAS - 1| <= M^2/8, the tolerance
  // used (higher-order terms are ~M^4/40, well inside). ISA 3000 m, TAS 34 m/s (M ~ 0.10).
  astra::AtmosphereModel& isa = load_isa();
  const AtmoState alt = isa.at(3000.0);
  const AtmoState sl = isa.at(0.0);
  const double tas = 34.0;
  const auto out =
      pack(context_at(1), level_state({tas, 0.0, 0.0}), env_with(kZero, alt), flat_stub());
  const double eas = tas * std::sqrt(alt.density_kg_m3 / sl.density_kg_m3);
  const double mach = tas / alt.speed_of_sound_m_s;
  EXPECT_LE(std::abs(out.cas / eas - 1.0), mach * mach / 8.0);
  EXPECT_GT(std::abs(out.cas / eas - 1.0), 0.0);  // it is not vacuous: CAS != EAS exactly
}

// ---- degenerate air data: flagged, defined, never NaN
// ----------------------------------------------

TEST(IcdPack, DefaultAllNanAtmosphereFlagsAndZeroesMachAndCas) {
  const auto out =
      pack(context_at(1), level_state({100.0, 0.0, 0.0}), EnvironmentSample{}, flat_stub());
  EXPECT_NE(out.status_flags & ASTRA_ICD_STATUS_ATMOSPHERE_INVALID, 0U);
  EXPECT_EQ(out.mach, 0.0);
  EXPECT_EQ(out.cas, 0.0);
  EXPECT_NEAR(out.tas, 100.0, kTol);  // tas needs no atmosphere
  EXPECT_EQ(out.status_flags & ASTRA_ICD_STATUS_WIND_INVALID, 0U);
  EXPECT_TRUE(all_fields_finite(out));
}

TEST(IcdPack, NonPositivePressureFlagsAtmosphere) {
  const auto out = pack(context_at(1), level_state({100.0, 0.0, 0.0}),
                        env_with(kZero, atmosphere(0.0, 340.0)), flat_stub());
  EXPECT_NE(out.status_flags & ASTRA_ICD_STATUS_ATMOSPHERE_INVALID, 0U);
  EXPECT_EQ(out.cas, 0.0);
}

TEST(IcdPack, SupersonicFlagsCasOnlyAndKeepsAirData) {
  // tas 330, a = 300 => M = 1.1 exactly.
  const auto out = pack(context_at(1), level_state({330.0, 0.0, 0.0}),
                        env_with(kZero, atmosphere(26436.0, 300.0)), flat_stub());
  EXPECT_NE(out.status_flags & ASTRA_ICD_STATUS_CAS_SUPERSONIC, 0U);
  EXPECT_EQ(out.status_flags & ASTRA_ICD_STATUS_ATMOSPHERE_INVALID, 0U);
  EXPECT_EQ(out.cas, 0.0);
  EXPECT_NEAR(out.mach, 1.1, 1e-12);
  EXPECT_NEAR(out.tas, 330.0, kTol);
}

TEST(IcdPack, NonFiniteWindFlagsAndZeroesEveryAirRelativeField) {
  const auto out = pack(
      context_at(1), level_state({100.0, 0.0, 5.0}),
      env_with({std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, atmosphere(26436.0, 299.53)),
      flat_stub());
  EXPECT_NE(out.status_flags & ASTRA_ICD_STATUS_WIND_INVALID, 0U);
  EXPECT_EQ(out.tas, 0.0);
  EXPECT_EQ(out.alpha, 0.0);
  EXPECT_EQ(out.beta, 0.0);
  EXPECT_EQ(out.mach, 0.0);
  EXPECT_EQ(out.cas, 0.0);
  EXPECT_EQ(out.u, 100.0);  // truth fields are untouched
  EXPECT_TRUE(all_fields_finite(out));
}

// ---- specific force ---------------------------------------------------------------------------

PreviousFrame previous_frame(const Eigen::Vector3d& force_b, double mass) {
  PreviousFrame previous;
  previous.wrench_total.force_b = force_b;
  previous.mass = mass;
  return previous;
}

TEST(IcdPack, LevelFlightReadsMinusGInBodyZ) {
  // IMU convention (z down): in level flight the lift/thrust balance gravity, wrench_total (gravity
  // excluded) is (0, 0, -m g), specific force = F/m = (0, 0, -g): the accelerometer reads +g UP.
  constexpr double kMass = 50000.0;
  const PreviousFrame previous = previous_frame({0.0, 0.0, -kMass * kStandardGravity}, kMass);
  const auto out = pack(context_at(4), level_state({100.0, 0.0, 0.0}),
                        env_with(kZero, atmosphere(1.0e5, 340.0)), flat_stub(), &previous);
  EXPECT_EQ(out.status_flags & ASTRA_ICD_STATUS_ACCEL_INVALID, 0U);
  EXPECT_NEAR(out.ax, 0.0, kTol);
  EXPECT_NEAR(out.ay, 0.0, kTol);
  EXPECT_NEAR(out.az, -kStandardGravity, 1e-9);
}

TEST(IcdPack, SpecificForceIsForceOverMassPerAxis) {
  // (1000, -200, -9000) N / 2000 kg = (0.5, -0.1, -4.5) m/s^2.
  const PreviousFrame previous = previous_frame({1000.0, -200.0, -9000.0}, 2000.0);
  const auto out = pack(context_at(9), level_state({100.0, 0.0, 0.0}),
                        env_with(kZero, atmosphere(1.0e5, 340.0)), flat_stub(), &previous);
  EXPECT_NEAR(out.ax, 0.5, kTol);
  EXPECT_NEAR(out.ay, -0.1, kTol);
  EXPECT_NEAR(out.az, -4.5, kTol);
}

TEST(IcdPack, FrameZeroIsInvalidEvenWithAPreviousWrench) {
  const PreviousFrame previous = previous_frame({1000.0, -200.0, -9000.0}, 2000.0);
  const auto out = pack(context_at(0), level_state({100.0, 0.0, 0.0}),
                        env_with(kZero, atmosphere(1.0e5, 340.0)), flat_stub(), &previous);
  EXPECT_NE(out.status_flags & ASTRA_ICD_STATUS_ACCEL_INVALID, 0U);
  EXPECT_EQ(out.ax, 0.0);
  EXPECT_EQ(out.ay, 0.0);
  EXPECT_EQ(out.az, 0.0);
  EXPECT_EQ(out.frame, 0U);
  EXPECT_EQ(out.t, 0.0);
}

TEST(IcdPack, MissingPreviousNonFiniteForceOrBadMassAreInvalid) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const StateVector y = level_state({100.0, 0.0, 0.0});
  const EnvironmentSample env = env_with(kZero, atmosphere(1.0e5, 340.0));
  const PreviousFrame nan_force = previous_frame({nan, 0.0, 0.0}, 1000.0);
  const PreviousFrame inf_force = previous_frame({0.0, inf, 0.0}, 1000.0);
  const PreviousFrame zero_mass = previous_frame({1.0, 1.0, 1.0}, 0.0);
  const PreviousFrame negative_mass = previous_frame({1.0, 1.0, 1.0}, -5.0);
  const PreviousFrame nan_mass = previous_frame({1.0, 1.0, 1.0}, nan);
  const PreviousFrame inf_mass = previous_frame({1.0, 1.0, 1.0}, inf);
  for (const PreviousFrame* previous :
       {static_cast<const PreviousFrame*>(nullptr), &nan_force, &inf_force, &zero_mass,
        &negative_mass, &nan_mass, &inf_mass}) {
    const auto out = pack(context_at(5), y, env, flat_stub(), previous);
    EXPECT_NE(out.status_flags & ASTRA_ICD_STATUS_ACCEL_INVALID, 0U);
    EXPECT_EQ(out.ax, 0.0);
    EXPECT_EQ(out.ay, 0.0);
    EXPECT_EQ(out.az, 0.0);
    EXPECT_TRUE(all_fields_finite(out));
  }
}

// ---- Earth model: flat and round, the stubs and the real plugins ------------------------------

TEST(IcdPack, FlatEarthLatLonAreZeroWithAFlagAndAltitudeIsMinusDown) {
  // p_n = NED metres; altitude = -p_n(2).
  const auto out = pack(
      context_at(1), make_state({1200.0, -800.0, -10000.0}, {100.0, 0.0, 0.0}, {1, 0, 0, 0}, kZero),
      env_with(kZero, atmosphere(26436.0, 299.53)), flat_stub());
  EXPECT_NE(out.status_flags & ASTRA_ICD_STATUS_LATLON_NOT_MODELED, 0U);
  EXPECT_EQ(out.lat, 0.0);
  EXPECT_EQ(out.lon, 0.0);
  EXPECT_EQ(out.altitude, 10000.0);
}

TEST(IcdPack, RoundEarthLatLonAndAltitudeArePNComponentsWithoutAFlag) {
  // p_n = (lat, lon, altitude) rad, rad, m.
  const auto out =
      pack(context_at(1), make_state({0.6, -1.2, 10000.0}, {100.0, 0.0, 0.0}, {1, 0, 0, 0}, kZero),
           env_with(kZero, atmosphere(26436.0, 299.53)), round_stub());
  EXPECT_EQ(out.status_flags & ASTRA_ICD_STATUS_LATLON_NOT_MODELED, 0U);
  EXPECT_EQ(out.lat, 0.6);
  EXPECT_EQ(out.lon, -1.2);
  EXPECT_EQ(out.altitude, 10000.0);
}

EarthModel& load_earth(std::string_view plugin_path, const char* yaml) {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded;
  loaded = loader.load(plugin_path);
  auto* const earth = dynamic_cast<EarthModel*>(loaded.model.get());
  astra::SignalRegistry registry;
  earth->init(astra::Config::from_string(yaml, "icd-pack-earth-fixture"), registry);
  return *earth;
}

TEST(IcdPack, VerticalSpeedEqualsTheRealEarthModelsAltitudeRate) {
  // The header comment's claim, checked against the real plugins: -(C_bn v_b)_z is the Earth
  // model's altitude rate: p_n_dot(2) for round and wgs84_rotating (which store altitude in
  // p_n(2), p_n_dot(2) = -v_n(2)), and -p_n_dot(2) for flat (p_n(2) is down, altitude = -p_n(2)).
  const auto& c = golden("composite");
  const Eigen::Vector3d v_b{100.0, 10.0, -5.0};
  const EnvironmentSample env = env_with(kZero, atmosphere(26436.0, 299.53));
  double v_down = 0.0;
  for (int i = 0; i < 3; ++i) {
    v_down += c.C_nb[i][2] * v_b(i);  // golden DCM, independent of the code under test
  }

  EarthModel& round = load_earth("plugins/physical/earth/round", "radius: 6371000.0\n");
  const StateVector y_geodetic = make_state({0.6, -1.2, 10000.0}, v_b, golden_q(c), kZero);
  const auto out_round = pack(context_at(1), y_geodetic, env, round);
  EXPECT_NEAR(out_round.vertical_speed, -v_down, kTol);
  EXPECT_NEAR(out_round.vertical_speed, round.p_n_dot(VehicleState{y_geodetic})(2), kTol);
  EXPECT_EQ(out_round.status_flags & ASTRA_ICD_STATUS_LATLON_NOT_MODELED, 0U);
  EXPECT_EQ(out_round.lat, 0.6);
  EXPECT_EQ(out_round.lon, -1.2);
  EXPECT_EQ(out_round.altitude, 10000.0);

  EarthModel& wgs84 = load_earth("plugins/physical/earth/wgs84_rotating", "{}\n");
  const auto out_wgs84 = pack(context_at(1), y_geodetic, env, wgs84);
  EXPECT_NEAR(out_wgs84.vertical_speed, -v_down, kTol);
  EXPECT_NEAR(out_wgs84.vertical_speed, wgs84.p_n_dot(VehicleState{y_geodetic})(2), kTol);
  EXPECT_EQ(out_wgs84.lat, 0.6);
  EXPECT_EQ(out_wgs84.lon, -1.2);
  EXPECT_EQ(out_wgs84.altitude, 10000.0);

  EarthModel& flat = load_earth("plugins/physical/earth/flat", "{}\n");
  const StateVector y_flat = make_state({1200.0, -800.0, -10000.0}, v_b, golden_q(c), kZero);
  const auto out_flat = pack(context_at(1), y_flat, env, flat);
  EXPECT_NEAR(out_flat.vertical_speed, -v_down, kTol);
  EXPECT_NEAR(out_flat.vertical_speed, -flat.p_n_dot(VehicleState{y_flat})(2), kTol);
  EXPECT_NE(out_flat.status_flags & ASTRA_ICD_STATUS_LATLON_NOT_MODELED, 0U);
  EXPECT_EQ(out_flat.altitude, 10000.0);
}

}  // namespace
