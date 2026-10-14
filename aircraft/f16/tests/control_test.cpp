// The F-16's sim-side F16_control peer against its source, NASA's F16_control.dml (M7 PR 11a;
// docs/plans/m7-pr11a-control-peer.md; the M7 plan's R11, R12 and R23). The plugin is
// sim/transport/nesc_control: control_law.hpp's SI law, run by nesc_control_peer.hpp on the ICD v1
// frame.
//
// Pre-registered in the plan (Q2, Q3, Q8, D5), and kept here:
// - GainsAndLimitsEqualTheDmlValuesTimesTheirConversions: each SI constant equals the DML's printed
//   value times its conversion, computed here from literal constants (pi/180, 0.3048, 1852/3600),
//   to 1e-15 relative.
// - MatchesTheDmlUnitsReference*: the SI law's ICD commands against control_reference.hpp, an
//   independent evaluation of the DML in its own units, followed by that file's own conversion, on
//   hand-built and on 10,000 seeded ICD v1 frames. On the hand-built frames, each command within
//   1e-12 * max(|c_ref|, F), with F its full scale in ICD units: 25, 21.5 and 30.172 deg in rad,
//   and throttle 1 (Q2). On the seeded frames, that floor plus the input-conditioning term of the
//   plan's Q2 deviations (Joe, 2026-10-13): each rounding step between a raw input and the
//   difference the law forms from it, charged relative to its own result and scaled by the law's
//   gain to the command, the gains derived from the reference's constants and checked by finite
//   difference on every frame. Three planted defects must still fail by at least 1e8 times that
//   bound. A seeded frame within 1e-9 * max(|b|, 1) of a saturation boundary or of chiErr's wrap,
//   by the reference's own pre-saturation values, is redrawn and counted (Q2's margin).
// - ReferenceMatchesTheMathMlEvaluation: both transcriptions against F16_control.dml's MathML,
//   evaluated by docs/reviews/evidence/m7-pr11a-law/mathml_commands.py, whose %.17g output is
//   pasted below (D5), at the same tolerance in the DML's units.
// - EarthRelativeRateSubtractsTheEarthRateInBodyAxes and CrossTrackOnLeftAndRightOfTheCourse: the
//   law's omega_ie_b subtraction and 13.4's initial-plane cross-track against independent
//   evaluations (R11, ADR-0029 Decisions 6 and 7).
// - EquivalentAirspeedEqualsTasTimesRootDensityRatio (R12): frames packed by
//   icd_pack::pack_sim_to_sw from the isa1976 plugin's own AtmoState; EAS against tas sqrt(rho /
//   rho0), rho that AtmoState's density, rho0 = p0 M0 / (R* T0) from icd_pack's constants, to 1e-12
//   relative (Q3).
// - RefusesEveryFlaggedFrame and FlaggedFramesHaltTheExchangeNamingTheField (R12, Q8).
// - DisengagedLawReproducesTheTrim (R23): within 1e-12.
// - InitRefusesMissingOrOutOfRangeKeys.
// A miss in any of these goes to Joe; no tolerance here is widened to clear one.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../sim/transport/nesc_control/control_law.hpp"
#include "../sim/transport/nesc_control/nesc_control_peer.hpp"
#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/atmosphere_model.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/transport.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/exchange_recorder.hpp"
#include "astra/kernel/health_monitor.hpp"
#include "astra/kernel/lockstep_exchange.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/kernel/virtual_clock.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/frames.hpp"
#include "astra/util/icd_pack.hpp"
#include "astra/util/random.hpp"
#include "astra/util/units.hpp"
#include "control_reference.hpp"
#include "icd/astra_icd.h"

namespace {

namespace law = astra::aircraft_f16_control_law;
namespace ref = f16_control_reference;
namespace units = astra::units;

constexpr std::string_view kPeer = "aircraft/f16/sim/transport/nesc_control";

// This test's own literal conversions, independent of core/util/units.
constexpr double kDeg = std::numbers::pi / 180.0;
constexpr double kFt = 0.3048;
constexpr double kKt = 1852.0 / 3600.0;

// Q2's floor: each ICD command's full scale.
constexpr double kElevatorScale = 25.0 * kDeg;
constexpr double kAileronScale = 21.5 * kDeg;
constexpr double kRudderScale = 30.172 * kDeg;
constexpr double kThrottleScale = 1.0;
constexpr double kRelative = 1e-12;

// Case 11's (and 13.x's) trim: aircraft/f16/configs/case11_trim_check_subsonic.yaml's literals.
constexpr double kTrimElevator = -0.05642226572274011;
constexpr double kTrimThrottle = 0.13873817846824657;
constexpr double kLat0 = 0.6286530522161018;
constexpr double kLon0 = -1.3207682150955165;
constexpr double kH0 = 3051.9624000000003;
constexpr std::array<double, 3> kTrimVb = {172.23806690598641, 0.0, 7.9385835316476205};
constexpr std::array<double, 3> kTrimEuler = {0.7853981633974483, 0.0460581645127969, 0.0};
constexpr std::array<double, 3> kTrimOmega = {4.421479346717576e-05, -6.875360966116474e-05,
                                              -5.4779192377020555e-05};

std::string read_text(const std::string& path) {
  std::ifstream in{path};
  if (!in) {
    throw astra::Error("cannot read " + path);
  }
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::string config_path(std::string_view file_name) {
  return std::string{ASTRA_F16_CONFIG_DIR} + "/" + std::string{file_name};
}

// --- Comparison ---------------------------------------------------------------------------------

struct Worst {
  double ratio = 0.0;
  void note(double got, double want, double scale) {
    ratio = std::max(ratio, std::abs(got - want) / (kRelative * std::max(std::abs(want), scale)));
  }
};

::testing::AssertionResult within(double got, double want, double scale, std::string_view what) {
  const double bound = kRelative * std::max(std::abs(want), scale);
  if (std::abs(got - want) <= bound) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure()
         << what << ": " << std::format("{:.17g}", got) << " against "
         << std::format("{:.17g}", want) << ", |d| " << std::abs(got - want) << " > " << bound;
}

void expect_commands(const law::IcdCommands& si, const ref::Icd& want, std::string_view where,
                     Worst* worst = nullptr) {
  EXPECT_TRUE(within(si.elevator, want.elevator, kElevatorScale, "elevator")) << where;
  EXPECT_TRUE(within(si.aileron, want.aileron, kAileronScale, "aileron")) << where;
  EXPECT_TRUE(within(si.rudder, want.rudder, kRudderScale, "rudder")) << where;
  EXPECT_TRUE(within(si.throttle, want.throttle, kThrottleScale, "throttle")) << where;
  if (worst != nullptr) {
    worst->note(si.elevator, want.elevator, kElevatorScale);
    worst->note(si.aileron, want.aileron, kAileronScale);
    worst->note(si.rudder, want.rudder, kRudderScale);
    worst->note(si.throttle, want.throttle, kThrottleScale);
  }
}

// --- Frames -------------------------------------------------------------------------------------

// The isa1976 and wgs84_rotating plugins as built, each loaded once.
template <typename Model>
struct LoadedModel {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded;
  Model* model = nullptr;

  explicit LoadedModel(std::string_view path) : loaded(loader.load(path)) {
    model = dynamic_cast<Model*>(loaded.model.get());
    if (model == nullptr) {
      throw astra::Error("unexpected plugin type: " + std::string{path});
    }
    astra::SignalRegistry registry;
    model->init(astra::Config::from_string("{}\n", "f16-control-fixture"), registry);
  }
};

astra::AtmosphereModel& isa() {
  static LoadedModel<astra::AtmosphereModel> fixture{"plugins/physical/atmosphere/isa1976"};
  return *fixture.model;
}

astra::EarthModel& wgs84() {
  static LoadedModel<astra::EarthModel> fixture{"plugins/physical/earth/wgs84_rotating"};
  return *fixture.model;
}

astra::StateVector make_state(const Eigen::Vector3d& p_n, const Eigen::Vector3d& v_b,
                              const astra::frames::EulerAngles& euler,
                              const Eigen::Vector3d& omega_b) {
  astra::StateVector y = astra::StateVector::Zero();
  y.segment<astra::state::kPositionSize>(astra::state::kPosition) = p_n;
  y.segment<astra::state::kVelocitySize>(astra::state::kVelocity) = v_b;
  y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion) =
      astra::frames::quaternion_from_euler(euler);
  y.segment<astra::state::kAngularRateSize>(astra::state::kAngularRate) = omega_b;
  return y;
}

// A frame packed by the core's own packer from a state, the isa1976 plugin's AtmoState at the
// state's altitude, still air, and the WGS-84 rotating Earth.
astra_sim_to_sw_v1 packed(const astra::StateVector& y,
                          astra::EnvironmentSample* env_out = nullptr) {
  astra::VirtualClock clock{0.001};
  const astra::VehicleState vs{y.data()};
  astra::EnvironmentSample env;
  env.wind_n = Eigen::Vector3d::Zero();
  env.atmosphere = isa().at(y(astra::state::kPosition + 2));
  astra_sim_to_sw_v1 frame{};
  astra::icd_pack::pack_sim_to_sw(frame, clock.context(), vs, env, wgs84(), nullptr);
  if (env_out != nullptr) {
    *env_out = env;
  }
  return frame;
}

astra::StateVector trim_state() {
  return make_state({kLat0, kLon0, kH0}, {kTrimVb[0], kTrimVb[1], kTrimVb[2]},
                    {kTrimEuler[0], kTrimEuler[1], kTrimEuler[2]},
                    {kTrimOmega[0], kTrimOmega[1], kTrimOmega[2]});
}

// One law evaluation, both ways, from one ICD frame and the commands in the DML's units.
struct Case {
  astra_sim_to_sw_v1 frame{};
  double alt_cmd_ft = 0.0;
  double keas_cmd = 0.0;
  double course_deg = 0.0;
  double lat_offset_ft = 0.0;
  double long_stk_trim = 0.0;
  double throttle_trim = 0.0;
  law::Modes modes{};
  // The LQR references, when not the DML's hard-coded defaults on both sides: the peer's decided
  // ones (the M7 plan's Q11 as amended 2026-10-13), given in SI and converted to the DML's units by
  // the reference's own constants.
  std::optional<law::References> refs;
};

law::IcdCommands si_commands(const Case& c) {
  law::Commands cmd;
  cmd.altitude = units::feet_to_meters(c.alt_cmd_ft);
  cmd.eas = units::knots_to_meters_per_second(c.keas_cmd);
  cmd.course = units::degrees_to_radians(c.course_deg);
  cmd.lateral_deviation_error = units::feet_to_meters(c.lat_offset_ft);
  const law::Trim trim{c.long_stk_trim, c.throttle_trim};
  return law::to_icd(law::evaluate(law::inputs_from(c.frame), cmd, trim, c.modes,
                                   c.refs.value_or(law::References{})));
}

ref::Inputs reference_inputs(const Case& c) {
  ref::Inputs in = ref::from_frame(c.frame, c.keas_cmd, c.alt_cmd_ft, c.course_deg, c.lat_offset_ft,
                                   c.throttle_trim, c.long_stk_trim);
  in.sas_on = c.modes.sas_on ? 1.0 : 0.0;
  in.ap_on = c.modes.ap_on ? 1.0 : 0.0;
  in.throttle = c.modes.throttle;
  in.long_stk = c.modes.long_stick;
  in.lat_stk = c.modes.lat_stick;
  in.pedal = c.modes.pedal;
  if (c.refs) {
    in.trimmed_alpha = c.refs->alpha / ref::kDegree;
    in.trimmed_theta = c.refs->theta / ref::kDegree;
    in.trimmed_keas = c.refs->eas / ref::kKnot;
  }
  return in;
}

constexpr std::array<std::string_view, 4> kCommandNames = {"elevator", "aileron", "rudder",
                                                           "throttle"};

// --- The seeded bound's input-conditioning term (the plan's Q2 deviations; Joe's 2026-10-13
// decisions) ---
//
// The law forms differences of its inputs before its gains: chiErr = (beta + psi) - (deltaChiCmd +
// course), altErr = altitude - altCmd, deltaTheta = theta - thetaCmd, deltaAlpha = alpha -
// trimmedAlpha, deltaV = Vequiv - keasCmd, deltaPhi = phi - phiCmd, and the body rate minus the
// Earth rate. The two implementations round those steps differently (degrees against radians,
// feet against meters), so the bound per frame and command is
//   1e-12 max(|c_ref|, F) + eps * sum over rounding steps of |that step's own result| * |G|,
// eps = 2^-52, where G is the gain from that step's quantity to the command. Each step is charged
// relative to its own result. The gains are derived below from the reference's own DML constants
// and this file's conversions (never read from the SI law), and checked against the reference's
// own finite differences on every frame (FdCheck). The steps, both implementations together:
// - psi, k = 3: reference psi/deg (|psi|); reference beta + psi (|chiEst|); SI beta + psi
//   (|chiEst|).
// - course, k = 3: reference deltaChiCmd + base (|chiCmd|); SI deg to rad (|course|); SI deltaChi
//   + course (|chiCmd|).
// - beta, k = 1: reference beta/deg (|beta|); the sums are psi's.
// - chiErr: chiEst - chiCmd on each side (|chiErrUnwrapped|), and one turn on each side when it
//   wraps (|chiErr|): 2, or 4 wrapped.
// - latOffset, k = 3: SI ft to m (|latOffset|); reference -0.01 latOffset and SI gain times offset
//   (|deltaChiCmd|).
// - phiCmd, k = 2: -10 chiErr on each side (|phiCmd|). phi, k = 1: reference phi/deg (|phi|).
//   deltaPhi: phi - phiCmd on each side (|deltaPhi|).
// - altitude, k = 1: reference alt/ft (|alt|). altCmd, k = 1: SI ft to m (|altCmd|). altErr: alt -
//   altCmd on each side (|altErr|). deltaThetaCmd: the gain times altErr on each side
//   (|deltaThetaCmd|).
// - theta, k = 1: reference theta/deg (|theta|). thetaCmd, k = 3: SI trimmedTheta deg to rad
//   (|trimmedTheta|); delta + trimmed on each side (|thetaCmd|). deltaTheta: theta - thetaCmd on
//   each side (|deltaTheta|).
// - alpha, k = 1: reference alpha/deg (|alpha|). trimmedAlpha, k = 1: SI deg to rad. deltaAlpha:
//   alpha - trimmedAlpha on each side (|deltaAlpha|).
// - Vequiv: inside the EAS route the two "pow - 1" cancellations amplify a step's rounding by
//   kappa = B^3.5 / (B^3.5 - 1), B the pow's base (about 143 at Mach 0.1); each step carries its
//   derived coefficient on ln V (eas_route_coefficient below), and the reference's division by the
//   knot adds 1 (|V|). keasCmd, k = 1: SI kt to m/s (|keasCmd|). deltaV: Vequiv - keasCmd on each
//   side (|deltaV|).
// - p, q and r: the Earth rate's subtraction on each side (|the Earth-relative rate|); the Earth
//   rate's own formation, 26 steps at most, each about Omega.
// A gain follows the law's own switches and the intermediate saturations it crosses (deltaThetaCmd
// +-5 deg, deltaChiCmd +-30 deg, phiCmd +-30 deg): past one, its derivative is zero. A saturated
// command (a stick total past +-1, the throttle total outside [0, 1]) is still charged with its
// unsaturated gain, which is conservative, and the finite-difference check skips it, counted.
constexpr double kEps = 0x1.0p-52;

// The raw inputs the gains are checked against, by finite difference.
enum Input : std::uint8_t {
  kPsi,
  kBeta,
  kCourse,
  kLatOffset,
  kPhi,
  kAltitude,
  kAltCmd,
  kTheta,
  kAlpha,
  kV,
  kKeasCmd,
  kP,
  kQ,
  kR,
  kInputCount
};
constexpr std::array<std::string_view, kInputCount> kInputNames = {
    "psi",   "beta",  "course", "latOffset", "phi", "altitude", "altCmd",
    "theta", "alpha", "Vequiv", "keasCmd",   "p",   "q",        "r"};

// Per input, the gain to each ICD command (elevator, aileron, rudder, throttle), per SI unit.
using Gains = std::array<std::array<double, 4>, kInputCount>;

// The reference's constants (F16_control.dml, as printed), per DML unit.
constexpr double kLongV = -0.063009074230494;  // per kt
constexpr double kLongA = 0.113230403179271;   // per deg
constexpr double kLongQ = 10.113432224566077;  // per rad/s
constexpr double kLongT = 3.154983341632913;   // per deg
constexpr double kThrV = 0.997260602961658;
constexpr double kThrA = -0.025467711176391;
constexpr double kThrQ = 1.213308488207827;
constexpr double kThrT = 0.208744369535208;
constexpr double kLatPhi = 3.078043941515770;  // per deg
constexpr double kLatB = 0.032365863044163;    // per deg
constexpr double kLatP = 4.557858908828332;    // per rad/s
constexpr double kLatR = 0.589443156647647;
constexpr double kDirPhi = -0.705817452754520;
constexpr double kDirB = -0.256362860634868;
constexpr double kDirP = -1.073666149713151;
constexpr double kDirR = 0.822114635953878;
constexpr double kOmegaEarth = 7.292115e-5;

// The derived coefficient sum of the EAS route's rounding steps, both implementations, on ln V:
// per side, c/a0 (s_c), (c/a0)^2 and 0.2 (c/a0)^2 (s_c/2 each), 1 + (1.75 kappa_c), pow (0.5
// kappa_c), - 1 (0.5); M^2 and 0.2 M^2 (s_m each), 1 + (1.75 kappa_m), pow (0.5 kappa_m), - 1
// (0.5); the ratio (0.5), the sqrt (1), a0 M (1) and the product (1). kappa = B^3.5 / (B^3.5 - 1);
// s_c = 0.7 r^2 B_c^2.5 / Q_c and s_m = 0.35 M^2 B_m^2.5 / Q_m. a0 is formed by the same expression
// on both sides, so it adds nothing.
double eas_route_coefficient(double cas, double mach) {
  const double a0 = std::sqrt(1.4 * (8314.32 / 28.9644) * 288.15);
  const double r = cas / a0;
  const double b_c = 1.0 + 0.2 * r * r;
  const double b_m = 1.0 + 0.2 * mach * mach;
  const double q_c = std::pow(b_c, 3.5) - 1.0;
  const double q_m = std::pow(b_m, 3.5) - 1.0;
  const double kappa_c = (q_c + 1.0) / q_c;
  const double kappa_m = (q_m + 1.0) / q_m;
  const double s_c = 0.7 * r * r * std::pow(b_c, 2.5) / q_c;
  const double s_m = 0.35 * mach * mach * std::pow(b_m, 2.5) / q_m;
  const double per_side = s_c + s_c / 2.0 + s_c / 2.0 + 1.75 * kappa_c + 0.5 * kappa_c + 0.5 + s_m +
                          s_m + 1.75 * kappa_m + 0.5 * kappa_m + 0.5 + 0.5 + 1.0 + 1.0 + 1.0;
  return 2.0 * per_side;
}

// The four commands' gains from the LQR's long and lateral differences, in the DML's units, to
// the ICD's: el = -25 deg totLongStk with totLongStk = trim - (K . d); aileron (ICD, +RWD) = 21.5
// deg totLatStk; rudder = -30 deg totPedal + 0.008 ail(+LWD); throttle = totThrottle.
struct Deltas {
  double dv = 0.0;      // deltaV, kt per SI unit
  double dalpha = 0.0;  // deltaAlpha, deg per SI unit
  double dq = 0.0;      // q, rad/s per SI unit
  double dtheta = 0.0;  // deltaTheta, deg per SI unit
  double dphi = 0.0;    // deltaPhi, deg per SI unit
  double dbeta = 0.0;   // beta, deg per SI unit
  double dp = 0.0;      // p, rad/s per SI unit
  double dr = 0.0;      // r, rad/s per SI unit
};

std::array<double, 4> command_gains(const Deltas& d, bool fsas) {
  if (!fsas) {
    return {0.0, 0.0, 0.0, 0.0};
  }
  const double long_sum = kLongV * d.dv + kLongA * d.dalpha + kLongQ * d.dq + kLongT * d.dtheta;
  const double thr_sum = kThrV * d.dv + kThrA * d.dalpha + kThrQ * d.dq + kThrT * d.dtheta;
  const double lat_sum = kLatPhi * d.dphi + kLatB * d.dbeta + kLatP * d.dp + kLatR * d.dr;
  const double dir_sum = kDirPhi * d.dphi + kDirB * d.dbeta + kDirP * d.dp + kDirR * d.dr;
  const double el = 25.0 * kDeg * long_sum;      // d(-25 deg (trim - long_sum))
  const double ail_lwd = 21.5 * kDeg * lat_sum;  // d(-21.5 deg (-lat_sum))
  const double rdr = 30.0 * kDeg * dir_sum + 0.008 * ail_lwd;
  return {el, -ail_lwd, rdr, -thr_sum};
}

// The gains at one frame, following the law's switches and intermediate saturations.
Gains gains_at(const ref::Inputs& in, const ref::Outputs& want) {
  const bool fsas = in.sas_on + in.ap_on > 0.5;
  const bool ap = in.ap_on > 0.5;
  const bool theta_cmd_live = ap && std::abs(want.delta_theta_cmd) < 5.0;
  const bool phi_cmd_live = ap && std::abs(want.phi_cmd) < 30.0;
  const bool chi_cmd_live = std::abs(want.delta_chi_cmd) < 30.0;
  const double chi_to_phi = phi_cmd_live ? 10.0 / kDeg : 0.0;  // deg of deltaPhi per rad of chiErr

  Gains g{};
  g.at(kPsi) = command_gains({.dphi = chi_to_phi}, fsas);
  g.at(kBeta) = command_gains({.dphi = chi_to_phi, .dbeta = 1.0 / kDeg}, fsas);
  g.at(kCourse) = command_gains({.dphi = -chi_to_phi}, fsas);
  g.at(kLatOffset) =
      command_gains({.dphi = chi_cmd_live ? chi_to_phi * 0.01 * kDeg / kFt : 0.0}, fsas);
  g.at(kPhi) = command_gains({.dphi = 1.0 / kDeg}, fsas);
  g.at(kAltitude) = command_gains({.dtheta = theta_cmd_live ? 0.05 / kFt : 0.0}, fsas);
  g.at(kAltCmd) = command_gains({.dtheta = theta_cmd_live ? -0.05 / kFt : 0.0}, fsas);
  g.at(kTheta) = command_gains({.dtheta = 1.0 / kDeg}, fsas);
  g.at(kAlpha) = command_gains({.dalpha = 1.0 / kDeg}, fsas);
  g.at(kV) = command_gains({.dv = 1.0 / kKt}, fsas);
  g.at(kKeasCmd) = command_gains({.dv = ap ? -1.0 / kKt : 0.0}, fsas);
  g.at(kP) = command_gains({.dp = 1.0}, fsas);
  g.at(kQ) = command_gains({.dq = 1.0}, fsas);
  g.at(kR) = command_gains({.dr = 1.0}, fsas);
  return g;
}

// The conditioning term per command (see above).
std::array<double, 4> conditioning(const Case& c, const ref::Inputs& in, const ref::Outputs& want,
                                   const Gains& g) {
  std::array<double, 4> term{};
  const auto charge = [&](double result_magnitude, Input input) {
    for (std::size_t k = 0; k < 4; ++k) {
      term.at(k) += kEps * std::abs(result_magnitude) * std::abs(g.at(input).at(k));
    }
  };
  const astra_sim_to_sw_v1& f = c.frame;
  const bool ap = in.ap_on > 0.5;
  const double chi_est = f.beta + f.psi;
  const double chi_cmd = std::clamp(want.delta_chi_cmd, -30.0, 30.0) * kDeg + c.course_deg * kDeg;
  const bool wrapped = std::abs(want.chi_err_unwrapped) > 180.0;
  double chi_err = want.chi_err_unwrapped;
  if (wrapped) {
    chi_err += chi_err > 0.0 ? -360.0 : 360.0;
  }

  // psi (3), course (3), beta (1), chiErr (2, or 4 wrapped), latOffset (3).
  charge(f.psi, kPsi);
  charge(chi_est, kPsi);
  charge(chi_est, kPsi);
  charge(chi_cmd, kCourse);
  charge(c.course_deg * kDeg, kCourse);
  charge(chi_cmd, kCourse);
  charge(f.beta, kBeta);
  for (int side = 0; side < 2; ++side) {
    charge(want.chi_err_unwrapped * kDeg, kPsi);
    if (wrapped) {
      charge(chi_err * kDeg, kPsi);
    }
  }
  charge(units::feet_to_meters(c.lat_offset_ft), kLatOffset);
  charge(want.delta_chi_cmd * kDeg, kCourse);
  charge(want.delta_chi_cmd * kDeg, kCourse);

  // phiCmd (2), phi (1), deltaPhi (2).
  const double phi_cmd = ap ? std::clamp(want.phi_cmd, -30.0, 30.0) * kDeg : 0.0;
  charge(want.phi_cmd * kDeg, kPhi);
  charge(want.phi_cmd * kDeg, kPhi);
  charge(f.phi, kPhi);
  charge(f.phi - phi_cmd, kPhi);
  charge(f.phi - phi_cmd, kPhi);

  // altitude (1), altCmd (1), altErr (2), deltaThetaCmd (2).
  const double alt_cmd = units::feet_to_meters(c.alt_cmd_ft);
  charge(f.altitude, kAltitude);
  charge(alt_cmd, kAltitude);
  charge(f.altitude - alt_cmd, kAltitude);
  charge(f.altitude - alt_cmd, kAltitude);
  charge(want.delta_theta_cmd * kDeg, kTheta);
  charge(want.delta_theta_cmd * kDeg, kTheta);

  // theta (1), thetaCmd (3), deltaTheta (2).
  const double theta_cmd =
      (ap ? std::clamp(want.delta_theta_cmd, -5.0, 5.0) : 0.0) * kDeg + in.trimmed_theta * kDeg;
  charge(f.theta, kTheta);
  charge(in.trimmed_theta * kDeg, kTheta);
  charge(theta_cmd, kTheta);
  charge(theta_cmd, kTheta);
  charge(f.theta - theta_cmd, kTheta);
  charge(f.theta - theta_cmd, kTheta);

  // alpha (1), trimmedAlpha (1), deltaAlpha (2).
  charge(f.alpha, kAlpha);
  charge(in.trimmed_alpha * kDeg, kAlpha);
  charge(f.alpha - in.trimmed_alpha * kDeg, kAlpha);
  charge(f.alpha - in.trimmed_alpha * kDeg, kAlpha);

  // Vequiv (the EAS route, then the knot), keasCmd (1), deltaV (2).
  const double eas = ref::keas_from(f.cas, f.mach) * kKt;
  const double eas_ref = ap ? units::knots_to_meters_per_second(c.keas_cmd) : in.trimmed_keas * kKt;
  charge(eas * eas_route_coefficient(f.cas, f.mach), kV);
  charge(eas, kV);
  charge(eas_ref, kV);
  charge(eas - eas_ref, kV);
  charge(eas - eas_ref, kV);

  // The rates: the subtraction on each side, and the Earth rate's own formation.
  charge(in.pb, kP);
  charge(in.pb, kP);
  charge(in.qb, kQ);
  charge(in.qb, kQ);
  charge(in.rb, kR);
  charge(in.rb, kR);
  for (const Input input : {kP, kQ, kR}) {
    charge(26.0 * kOmegaEarth, input);
  }
  return term;
}

// The bound per command: Q2's floor plus the conditioning term.
std::array<double, 4> bounds(const ref::Icd& want, const std::array<double, 4>& term) {
  return {kRelative * std::max(std::abs(want.elevator), kElevatorScale) + term[0],
          kRelative * std::max(std::abs(want.aileron), kAileronScale) + term[1],
          kRelative * std::max(std::abs(want.rudder), kRudderScale) + term[2],
          kRelative * std::max(std::abs(want.throttle), kThrottleScale) + term[3]};
}

// --- The gains' finite-difference check (Joe's 2026-10-13 decision, item 2) ---
//
// Each gain against the reference's own central difference at the frame, within 1 %. The step is
// 1e-6 of each input's full scale (kFdStep). Where a kink (a saturation, or chiErr's wrap) lies
// within ten steps of the frame on one side, the difference is one-sided on the other, open side;
// where both sides hold one, the input is skipped. A command saturated at the frame skips the
// check for that command. All three branches are counted.
constexpr std::array<double, kInputCount> kFullScale = {std::numbers::pi,
                                                        10.0 * kDeg,
                                                        std::numbers::pi,
                                                        units::feet_to_meters(4000.0),
                                                        60.0 * kDeg,
                                                        11000.0,
                                                        11000.0,
                                                        30.0 * kDeg,
                                                        30.0 * kDeg,
                                                        300.0,
                                                        300.0,
                                                        3.0,
                                                        3.0,
                                                        3.0};
constexpr double kFdStep = 1e-6;

// Perturbs one input of the reference by dx (SI units), in the reference's own units.
ref::Inputs perturbed(ref::Inputs in, Input input, double dx) {
  switch (input) {
    case kPsi:
      in.psi += dx / kDeg;
      break;
    case kBeta:
      in.beta += dx / kDeg;
      break;
    case kCourse:
      in.base_chi_cmd += dx / kDeg;
      break;
    case kLatOffset:
      in.lat_offset += dx / kFt;
      break;
    case kPhi:
      in.phi += dx / kDeg;
      break;
    case kAltitude:
      in.alt_msl += dx / kFt;
      break;
    case kAltCmd:
      in.alt_cmd += dx / kFt;
      break;
    case kTheta:
      in.theta += dx / kDeg;
      break;
    case kAlpha:
      in.alpha += dx / kDeg;
      break;
    case kV:
      in.v_equiv += dx / kKt;
      break;
    case kKeasCmd:
      in.keas_cmd += dx / kKt;
      break;
    case kP:
      in.pb += dx;
      break;
    case kQ:
      in.qb += dx;
      break;
    case kR:
      in.rb += dx;
      break;
    case kInputCount:
      break;
  }
  return in;
}

std::array<double, 4> icd_of(const ref::Inputs& in) {
  const ref::Icd icd = ref::to_icd(ref::evaluate(in));
  return {icd.elevator, icd.aileron, icd.rudder, icd.throttle};
}

// The kink quantities, each with its boundaries, from a reference evaluation.
std::array<std::pair<double, std::array<double, 2>>, 8> kinks(const ref::Outputs& o) {
  return {{{o.delta_theta_cmd, {-5.0, 5.0}},
           {o.delta_chi_cmd, {-30.0, 30.0}},
           {o.phi_cmd, {-30.0, 30.0}},
           {o.chi_err_unwrapped, {-180.0, 180.0}},
           {o.tot_long_stk, {-1.0, 1.0}},
           {o.tot_lat_stk, {-1.0, 1.0}},
           {o.tot_pedal, {-1.0, 1.0}},
           {o.tot_throttle, {0.0, 1.0}}}};
}

// True when some kink quantity crosses one of its boundaries between in and in + 10 steps.
bool crosses_within_ten_steps(const ref::Inputs& in, Input input, double h) {
  const auto at_frame = kinks(ref::evaluate(in));
  const auto ten_away = kinks(ref::evaluate(perturbed(in, input, 10.0 * h)));
  for (std::size_t k = 0; k < at_frame.size(); ++k) {
    for (const double b : at_frame.at(k).second) {
      if ((at_frame.at(k).first - b) * (ten_away.at(k).first - b) <= 0.0) {
        return true;
      }
    }
  }
  return false;
}

struct FdCounts {
  int central = 0;
  int one_sided = 0;
  int skipped_both_sides = 0;
  int skipped_saturated = 0;
  int checked = 0;
  double largest_relative = 0.0;
};

void fd_check(const ref::Inputs& in, const ref::Outputs& want, const Gains& g,
              std::string_view where, FdCounts& counts) {
  const std::array<bool, 4> saturated = {
      std::abs(want.tot_long_stk) >= 1.0, std::abs(want.tot_lat_stk) >= 1.0,
      std::abs(want.tot_pedal) >= 1.0 || std::abs(want.tot_lat_stk) >= 1.0,
      want.tot_throttle <= 0.0 || want.tot_throttle >= 1.0};
  for (std::size_t i = 0; i < kInputCount; ++i) {
    const auto input = static_cast<Input>(i);
    const double h = kFdStep * kFullScale.at(i);
    const bool up = crosses_within_ten_steps(in, input, h);
    const bool down = crosses_within_ten_steps(in, input, -h);
    std::array<double, 4> fd{};
    if (up && down) {
      ++counts.skipped_both_sides;
      continue;
    }
    const std::array<double, 4> c0 = icd_of(in);
    if (!up && !down) {
      const std::array<double, 4> hi = icd_of(perturbed(in, input, h));
      const std::array<double, 4> lo = icd_of(perturbed(in, input, -h));
      for (std::size_t k = 0; k < 4; ++k) {
        fd.at(k) = (hi.at(k) - lo.at(k)) / (2.0 * h);
      }
      ++counts.central;
    } else {
      const double step = up ? -h : h;  // the open side
      const std::array<double, 4> moved = icd_of(perturbed(in, input, step));
      for (std::size_t k = 0; k < 4; ++k) {
        fd.at(k) = (moved.at(k) - c0.at(k)) / step;
      }
      ++counts.one_sided;
    }
    for (std::size_t k = 0; k < 4; ++k) {
      if (saturated.at(k)) {
        ++counts.skipped_saturated;
        continue;
      }
      const double analytic = g.at(i).at(k);
      // The difference's own roundoff: a few ulps of the command over the step.
      const double resolution = 8.0 * kEps * std::max(std::abs(c0.at(k)), 1.0) / h;
      const double miss = std::abs(fd.at(k) - analytic);
      EXPECT_LE(miss, 0.01 * std::abs(analytic) + resolution)
          << where << ": d " << kCommandNames.at(k) << " / d " << kInputNames.at(i) << " "
          << std::format("{:.9g} by finite difference, {:.9g} derived", fd.at(k), analytic);
      if (std::abs(analytic) > 0.0) {
        counts.largest_relative = std::max(counts.largest_relative, miss / std::abs(analytic));
      }
      ++counts.checked;
    }
  }
}

Case engaged_trim_case() {
  Case c;
  c.frame = packed(trim_state());
  c.alt_cmd_ft = 10013.0;
  c.keas_cmd = ref::keas_from(c.frame.cas, c.frame.mach);
  c.course_deg = 45.0;
  c.long_stk_trim = law::trim_from_controls(kTrimElevator, kTrimThrottle).long_stick;
  c.throttle_trim = kTrimThrottle;
  return c;
}

// --- The peer through its plugin --------------------------------------------------------------

std::string peer_block(double elevator = kTrimElevator, double throttle = kTrimThrottle,
                       double keas = 287.98158203, double theta = kTrimEuler[1]) {
  return std::format(
      "peer:\n"
      "  trim: {{elevator: {:.17g}, throttle: {:.17g}, theta: {:.17g}}}\n"
      "  commands: {{altitude_ft: 10013, equivalent_airspeed_knots: {:.17g}, course_deg: 45}}\n",
      elevator, throttle, theta, keas);
}

// The transport block for a config: the exchange's sim-owned keys and the peer's.
std::string transport_block(double elevator = kTrimElevator, double throttle = kTrimThrottle,
                            std::string_view extra_peer = "", std::string_view plugin = kPeer) {
  std::string peer = peer_block(elevator, throttle) + std::string{extra_peer};
  std::string indented;
  std::istringstream lines{peer};
  for (std::string line; std::getline(lines, line);) {
    indented += "  " + line + "\n";
  }
  return std::format(
             "transport:\n"
             "  plugin: {}\n"
             "  divisor: 10\n"
             "  commands: {{elevator: controls.elevator, aileron: controls.aileron, rudder: "
             "controls.rudder, throttle: [controls.throttle, controls.throttle]}}\n"
             "  limits: {{elevator: [{:.17g}, {:.17g}], aileron: [{:.17g}, {:.17g}], rudder: "
             "[{:.17g}, {:.17g}], throttle: [0.0, 1.0]}}\n",
             plugin, -law::kElevatorRange, law::kElevatorRange, -law::kAileronRange,
             law::kAileronRange, -law::kRudderRange, law::kRudderRange) +
         indented;
}

struct LoadedPeer {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin plugin;
  astra::Transport* transport = nullptr;
  astra::SignalRegistry registry;

  explicit LoadedPeer(const std::string& block, std::string_view path = kPeer)
      : LoadedPeer(astra::Config::from_string(block, "f16-control-fixture"), path) {}

  // A transport block as a config holds it (a tracked config's `transport`).
  explicit LoadedPeer(const astra::Config& block, std::string_view path = kPeer) {
    plugin = loader.load(path);
    transport = dynamic_cast<astra::Transport*>(plugin.model.get());
    if (transport == nullptr) {
      throw astra::Error("the peer is not a Transport");
    }
    transport->init(block, registry);
    static_cast<void>(transport->open(astra::TransportSettings{}));
  }

  astra_sw_to_sim_v1 exchange(const astra_sim_to_sw_v1& request, std::uint32_t seq) const {
    transport->publish(request, seq);
    const astra::AckObservation ack = transport->wait_ack(seq - 1U);
    EXPECT_EQ(ack.status, astra::AckStatus::kObserved);
    EXPECT_EQ(ack.ack_seq, seq);
    astra_sw_to_sim_v1 reply{};
    transport->read_reply(reply);
    return reply;
  }

  [[nodiscard]] double signal(std::string_view name) const {
    const double* value = registry.find(name);
    if (value == nullptr) {
      throw astra::Error("no signal " + std::string{name});
    }
    return *value;
  }
};

bool all_non_finite(const astra_sw_to_sim_v1& reply) {
  return !std::isfinite(reply.elevator) && !std::isfinite(reply.aileron) &&
         !std::isfinite(reply.rudder) && !std::isfinite(reply.throttle[0]) &&
         !std::isfinite(reply.throttle[1]);
}

}  // namespace

// R11: every SI gain and limit is the DML's value times its stated conversion.
TEST(F16Control, GainsAndLimitsEqualTheDmlValuesTimesTheirConversions) {
  struct Constant {
    std::string_view name;
    double si;
    double expected;
  };
  const std::array<Constant, 33> constants = {{
      {"kAltitudeErrorGain", law::kAltitudeErrorGain, -0.05 * kDeg / kFt},
      {"kDeltaThetaLimit", law::kDeltaThetaLimit, 5.0 * kDeg},
      {"kLateralOffsetGain", law::kLateralOffsetGain, -0.01 * kDeg / kFt},
      {"kDeltaChiLimit", law::kDeltaChiLimit, 30.0 * kDeg},
      {"kTrackErrorGain", law::kTrackErrorGain, -10.0},
      {"kPhiLimit", law::kPhiLimit, 30.0 * kDeg},
      {"kHalfTurn", law::kHalfTurn, 180.0 * kDeg},
      {"kFullTurn", law::kFullTurn, 360.0 * kDeg},
      {"trimmedAlpha", law::References{}.alpha, 2.653813535191715 * kDeg},
      {"trimmedTheta", law::References{}.theta, 2.653813535191715 * kDeg},
      {"trimmedKEAS", law::References{}.eas, 287.8088596053291 * kKt},
      {"longLQR11", law::kLong.first, -0.063009074230494 / kKt},
      {"longLQR12", law::kLong.second, 0.113230403179271 / kDeg},
      {"longLQR13", law::kLong.third, 10.113432224566077},
      {"longLQR14", law::kLong.fourth, 3.154983341632913 / kDeg},
      {"longLQR21", law::kThrottle.first, 0.997260602961658 / kKt},
      {"longLQR22", law::kThrottle.second, -0.025467711176391 / kDeg},
      {"longLQR23", law::kThrottle.third, 1.213308488207827},
      {"longLQR24", law::kThrottle.fourth, 0.208744369535208 / kDeg},
      {"latdLQR11", law::kLat.first, 3.078043941515770 / kDeg},
      {"latdLQR12", law::kLat.second, 0.032365863044163 / kDeg},
      {"latdLQR13", law::kLat.third, 4.557858908828332},
      {"latdLQR14", law::kLat.fourth, 0.589443156647647},
      {"latdLQR21", law::kDir.first, -0.705817452754520 / kDeg},
      {"latdLQR22", law::kDir.second, -0.256362860634868 / kDeg},
      {"latdLQR23", law::kDir.third, -1.073666149713151},
      {"latdLQR24", law::kDir.fourth, 0.822114635953878},
      {"el per stick", law::kElevatorPerStick, -25.0 * kDeg},
      {"ail per stick", law::kAileronPerStick, -21.5 * kDeg},
      {"rdr per pedal", law::kRudderPerPedal, -30.0 * kDeg},
      {"ail to rdr", law::kAileronToRudder, 0.008},
      {"PWR per throttle", law::kPlaPerThrottle, 100.0},
      {"rudder range", law::kRudderRange, 30.0 * kDeg + 0.008 * 21.5 * kDeg},
  }};
  for (const Constant& c : constants) {
    EXPECT_LE(std::abs(c.si - c.expected), 1e-15 * std::abs(c.expected))
        << c.name << ": " << std::format("{:.17g} against {:.17g}", c.si, c.expected);
  }
  EXPECT_LE(std::abs(law::kElevatorRange - 25.0 * kDeg), 1e-15 * 25.0 * kDeg);
  EXPECT_LE(std::abs(law::kAileronRange - 21.5 * kDeg), 1e-15 * 21.5 * kDeg);
}

// R11 on hand-built frames: the trim at engagement, each 13.x step, the law disengaged, the
// pilot's inputs with SAS only, and chiErr's wrap.
TEST(F16Control, MatchesTheDmlUnitsReferenceOnHandBuiltFrames) {
  std::vector<std::pair<std::string, Case>> cases;
  const Case engaged = engaged_trim_case();
  cases.emplace_back("engagement", engaged);
  Case c = engaged;
  c.alt_cmd_ft = 10113.0;
  cases.emplace_back("13.1 step", c);
  c = engaged;
  c.keas_cmd -= 5.0;
  cases.emplace_back("13.2 step", c);
  c = engaged;
  c.course_deg = 60.0;
  cases.emplace_back("13.3 step", c);
  c = engaged;
  c.lat_offset_ft = (1.016 - 609.6) / kFt;
  cases.emplace_back("13.4 step", c);
  c = engaged;
  c.modes = law::Modes{false, false, 0.0, 0.0, 0.0, 0.0};
  cases.emplace_back("disengaged", c);
  c = engaged;
  c.modes = law::Modes{true, false, 0.3, 0.2, -0.004, 0.001};
  cases.emplace_back("SAS only, pilot inputs", c);
  // chiErr's wrap: heading -170 deg against a 170 deg course.
  astra::StateVector y = trim_state();
  y.segment<astra::state::kQuaternionSize>(astra::state::kQuaternion) =
      astra::frames::quaternion_from_euler({-170.0 * kDeg, kTrimEuler[1], 0.0});
  c = engaged;
  c.frame = packed(y);
  c.course_deg = 170.0;
  cases.emplace_back("track-error wrap", c);

  for (const auto& [name, k] : cases) {
    const ref::Outputs want = ref::evaluate(reference_inputs(k));
    expect_commands(si_commands(k), ref::to_icd(want), name);
    std::cout << std::format("{}: el {:.6f} ail(+LWD) {:.6f} rdr {:.6f} deg, PWR {:.6f}\n", name,
                             want.el, want.ail, want.rdr, want.pwr);
  }
}

// R11 on 10,000 seeded frames, every mode, with Q2's margin. Two regimes, alternating:
// - near the design point, perturbations sized so the LQR's sums stay inside their limits, which
//   is where the comparison discriminates;
// - wide, over and beyond 13.x's envelope, where most sticks saturate.
// The pilot's inputs are drawn inside their own limits: past them, an input is saturated before it
// reaches the totals, which then sit exactly on Q2's boundaries in the SAS-off modes. Those frames
// are hand-built instead ("pilot inputs past their limits" in the MathML anchor).
TEST(F16Control, MatchesTheDmlUnitsReferenceOnSeededFrames) {
  astra::random::Rng rng(20261013U);
  Worst worst;
  int redrawn = 0;
  int saturated_frames = 0;
  std::array<int, 24> redrawn_at{};
  std::array<std::string_view, 24> redrawn_name{};
  double largest_bound_ratio = 0.0;
  std::array<double, 4> largest_term_near{};
  std::array<double, 4> largest_term_wide{};
  FdCounts fd;
  constexpr int kDraws = 10000;
  constexpr double kDesign = 2.653813535191715;  // deg, the DML's trimmedAlpha and trimmedTheta
  for (int i = 0; i < kDraws; ++i) {
    const bool near_design = i % 2 == 0;
    for (;;) {
      Case c;
      astra_sim_to_sw_v1& f = c.frame;
      f.magic = ASTRA_ICD_MAGIC;
      f.version = ASTRA_ICD_VERSION;
      f.lat = rng.uniform(-89.95, 89.95) * kDeg;
      f.lon = rng.uniform(-180.0, 180.0) * kDeg;
      f.altitude = rng.uniform(0.0, 11000.0);
      f.psi = rng.uniform(-180.0, 180.0) * kDeg;
      f.mach = rng.uniform(0.1, 0.95);
      const double pressure = rng.uniform(2.0e4, 1.1e5);
      f.cas = astra::icd_pack::calibrated_airspeed(pressure, f.mach);
      f.tas = f.mach * rng.uniform(290.0, 341.0);
      const double keas = ref::keas_from(f.cas, f.mach);
      const double mode = rng.uniform(0.0, 1.0);
      c.modes.sas_on = mode < 0.8;
      c.modes.ap_on = mode < 0.6 || mode >= 0.9;
      if (near_design) {
        f.phi = rng.uniform(-0.1, 0.1) * kDeg;
        f.beta = rng.uniform(-0.05, 0.05) * kDeg;
        f.alpha = (kDesign + rng.uniform(-0.1, 0.1)) * kDeg;
        f.theta = (kDesign + rng.uniform(-0.1, 0.1)) * kDeg;
        f.p = rng.uniform(-0.005, 0.005);
        f.q = rng.uniform(-0.005, 0.005);
        f.r = rng.uniform(-0.005, 0.005);
        c.alt_cmd_ft = f.altitude / kFt + rng.uniform(-1.0, 1.0);
        c.keas_cmd = keas + rng.uniform(-0.2, 0.2);
        // A course within 0.005 deg of the track estimate, so phiCmd stays inside its limit.
        c.course_deg = (f.beta + f.psi) / kDeg + rng.uniform(-0.005, 0.005);
        c.lat_offset_ft = rng.uniform(-0.3, 0.3);
        c.long_stk_trim = rng.uniform(-0.3, 0.3);
        c.throttle_trim = rng.uniform(0.3, 0.7);
        c.modes.throttle = rng.uniform(0.0, 0.2);
        c.modes.long_stick = rng.uniform(-0.2, 0.2);
        c.modes.lat_stick = rng.uniform(-0.2, 0.2);
        c.modes.pedal = rng.uniform(-0.2, 0.2);
      } else {
        f.phi = rng.uniform(-60.0, 60.0) * kDeg;
        f.beta = rng.uniform(-10.0, 10.0) * kDeg;
        f.alpha = rng.uniform(-10.0, 30.0) * kDeg;
        f.theta = rng.uniform(-30.0, 30.0) * kDeg;
        f.p = rng.uniform(-3.0, 3.0);
        f.q = rng.uniform(-3.0, 3.0);
        f.r = rng.uniform(-3.0, 3.0);
        c.alt_cmd_ft = f.altitude / kFt + rng.uniform(-300.0, 300.0);
        c.keas_cmd = keas + rng.uniform(-20.0, 20.0);
        c.course_deg = rng.uniform(0.0, 360.0);
        c.lat_offset_ft = rng.uniform(-4000.0, 4000.0);
        c.long_stk_trim = rng.uniform(-0.5, 0.5);
        c.throttle_trim = rng.uniform(0.0, 1.0);
        c.modes.throttle = rng.uniform(0.0, 1.0);
        c.modes.long_stick = rng.uniform(-1.0, 1.0);
        c.modes.lat_stick = rng.uniform(-1.0, 1.0);
        c.modes.pedal = rng.uniform(-1.0, 1.0);
      }

      const ref::Inputs in = reference_inputs(c);
      const ref::Outputs want = ref::evaluate(in);
      // Q2's margin, on the reference's own values before each saturation or switch.
      const auto near = [](double s, double b) {
        return std::abs(s - b) <= 1e-9 * std::max(std::abs(b), 1.0);
      };
      // Each boundary, named, so the log shows which one a redrawn frame sat on.
      const std::array<std::pair<std::string_view, bool>, 24> boundaries = {{
          {"deltaThetaCmd +5", near(want.delta_theta_cmd, 5.0)},
          {"deltaThetaCmd -5", near(want.delta_theta_cmd, -5.0)},
          {"deltaChiCmd +30", near(want.delta_chi_cmd, 30.0)},
          {"deltaChiCmd -30", near(want.delta_chi_cmd, -30.0)},
          {"phiCmd +30", near(want.phi_cmd, 30.0)},
          {"phiCmd -30", near(want.phi_cmd, -30.0)},
          {"chiErr wrap +180", near(want.chi_err_unwrapped, 180.0)},
          {"chiErr wrap -180", near(want.chi_err_unwrapped, -180.0)},
          {"totLongStk +1", near(want.tot_long_stk, 1.0)},
          {"totLongStk -1", near(want.tot_long_stk, -1.0)},
          {"totLatStk +1", near(want.tot_lat_stk, 1.0)},
          {"totLatStk -1", near(want.tot_lat_stk, -1.0)},
          {"totPedal +1", near(want.tot_pedal, 1.0)},
          {"totPedal -1", near(want.tot_pedal, -1.0)},
          {"totThrottle 0", near(want.tot_throttle, 0.0)},
          {"totThrottle 1", near(want.tot_throttle, 1.0)},
          {"throttle 0", near(in.throttle, 0.0)},
          {"throttle 1", near(in.throttle, 1.0)},
          {"longStk +1", near(in.long_stk, 1.0)},
          {"longStk -1", near(in.long_stk, -1.0)},
          {"latStk +1", near(in.lat_stk, 1.0)},
          {"latStk -1", near(in.lat_stk, -1.0)},
          {"pedal +1", near(in.pedal, 1.0)},
          {"pedal -1", near(in.pedal, -1.0)},
      }};
      bool excluded = false;
      for (std::size_t b = 0; b < boundaries.size(); ++b) {
        if (boundaries.at(b).second) {
          excluded = true;
          ++redrawn_at.at(b);
          redrawn_name.at(b) = boundaries.at(b).first;
        }
      }
      if (excluded) {
        ++redrawn;
        continue;
      }
      if (std::abs(want.tot_long_stk) > 1.0 || std::abs(want.tot_lat_stk) > 1.0 ||
          std::abs(want.tot_pedal) > 1.0) {
        ++saturated_frames;
      }
      const ref::Icd want_icd = ref::to_icd(want);
      const law::IcdCommands got = si_commands(c);
      const Gains g = gains_at(in, want);
      const std::array<double, 4> term = conditioning(c, in, want, g);
      const std::array<double, 4> bound = bounds(want_icd, term);
      fd_check(in, want, g, std::format("draw {}", i), fd);
      const std::array<double, 4> si{got.elevator, got.aileron, got.rudder, got.throttle};
      const std::array<double, 4> rf{want_icd.elevator, want_icd.aileron, want_icd.rudder,
                                     want_icd.throttle};
      const std::array<double, 4> scale{kElevatorScale, kAileronScale, kRudderScale,
                                        kThrottleScale};
      for (std::size_t k = 0; k < 4; ++k) {
        const double floor = kRelative * std::max(std::abs(rf.at(k)), scale.at(k));
        std::array<double, 4>& by_regime = near_design ? largest_term_near : largest_term_wide;
        by_regime.at(k) = std::max(by_regime.at(k), term.at(k) / floor);
        const double d = std::abs(si.at(k) - rf.at(k));
        EXPECT_LE(d, bound.at(k)) << std::format(
            "draw {}: {} {:.17g} against {:.17g}, |d| {:.3g} > bound {:.3g}", i,
            kCommandNames.at(k), si.at(k), rf.at(k), d, bound.at(k));
        largest_bound_ratio = std::max(largest_bound_ratio, d / bound.at(k));
        worst.ratio = std::max(worst.ratio, d / floor);
      }
      break;
    }
  }
  std::cout << std::format(
      "F16Control.MatchesTheDmlUnitsReferenceOnSeededFrames: {} draws, {} redrawn inside Q2's "
      "margin, {} with a saturated stick\n"
      "  largest |d| / bound {:.3g}; largest |d| / floor {:.3g}\n"
      "  largest conditioning term / floor, near the design point: elevator {:.3g}, aileron "
      "{:.3g}, "
      "rudder {:.3g}, throttle {:.3g}\n"
      "  largest conditioning term / floor, wide (a saturated command is charged its unsaturated "
      "gain): elevator {:.3g}, aileron {:.3g}, rudder {:.3g}, throttle {:.3g}\n"
      "  rounding steps k: psi 3, course 3, beta 1, chiErr 2 (4 wrapped), latOffset 3, phiCmd 2, "
      "phi 1, deltaPhi 2, altitude 1, altCmd 1, altErr 2, deltaThetaCmd 2, theta 1, thetaCmd 3, "
      "deltaTheta 2, alpha 1, trimmedAlpha 1, deltaAlpha 2, keasCmd 1, deltaV 2, Vequiv the EAS "
      "route's derived coefficients + 1, p, q, r 2 each, the Earth rate 26\n"
      "  finite-difference gain check: {} central, {} one-sided, {} inputs skipped (a kink within "
      "ten steps on both sides), {} saturated commands skipped; {} gains checked, largest relative "
      "difference {:.3g} (within 1 %)\n",
      kDraws, redrawn, saturated_frames, largest_bound_ratio, worst.ratio, largest_term_near.at(0),
      largest_term_near.at(1), largest_term_near.at(2), largest_term_near.at(3),
      largest_term_wide.at(0), largest_term_wide.at(1), largest_term_wide.at(2),
      largest_term_wide.at(3), fd.central, fd.one_sided, fd.skipped_both_sides,
      fd.skipped_saturated, fd.checked, fd.largest_relative);
  for (std::size_t b = 0; b < redrawn_at.size(); ++b) {
    if (redrawn_at.at(b) > 0) {
      std::cout << std::format("  redrawn at {}: {}\n", redrawn_name.at(b), redrawn_at.at(b));
    }
  }
  // The plan's stop condition: exclusions "beyond a handful".
  EXPECT_LE(redrawn, 5);
  EXPECT_GT(saturated_frames, kDraws / 10) << "the draws must reach the saturations";
  EXPECT_LT(saturated_frames, kDraws - (kDraws / 10)) << "the draws must also stay unsaturated";
  EXPECT_GT(fd.checked, kDraws) << "the gain check must run";

  // Joe's item 3 (kept): a unit or sign defect still fails by at least 1e8 times the bound, the
  // conditioning term included, on one planted frame each. The frame is 13.x's trim at engagement
  // with a small roll and a 2 ft altitude command, so every command it reads is unsaturated.
  Case planted = engaged_trim_case();
  planted.frame.phi = 0.02 * kDeg;
  planted.alt_cmd_ft = 10015.0;
  const ref::Inputs pin = reference_inputs(planted);
  const ref::Outputs pout = ref::evaluate(pin);
  ASSERT_LT(std::abs(pout.tot_long_stk), 1.0);
  ASSERT_LT(std::abs(pout.tot_lat_stk), 1.0);
  ASSERT_LT(std::abs(pout.delta_theta_cmd), 5.0);
  const ref::Icd pwant = ref::to_icd(pout);
  const law::IcdCommands pgot = si_commands(planted);
  const std::array<double, 4> pbound =
      bounds(pwant, conditioning(planted, pin, pout, gains_at(pin, pout)));
  const ref::Icd deg_rad =
      ref::to_icd(ref::evaluate(pin, ref::Plant{.long_theta_gain_factor = kDeg}));
  const ref::Icd ft_m = ref::to_icd(ref::evaluate(pin, ref::Plant{.altitude_gain_factor = kFt}));
  const double unflipped_aileron = -pwant.aileron;  // the boundary's sign flip omitted
  const std::array<std::tuple<std::string_view, double, double, double>, 3> plants = {{
      {"deg <-> rad on longLQR14 (elevator)", deg_rad.elevator, pgot.elevator, pbound.at(0)},
      {"the aileron's flip omitted (aileron)", unflipped_aileron, pgot.aileron, pbound.at(1)},
      {"ft <-> m on the altitude gain (elevator)", ft_m.elevator, pgot.elevator, pbound.at(0)},
  }};
  for (const auto& [name, defect, si_command, bound] : plants) {
    const double ratio = std::abs(defect - si_command) / bound;
    EXPECT_GE(ratio, 1e8) << name;
    std::cout << std::format("  planted {}: |d| / bound {:.3g} (at least 1e8)\n", name, ratio);
  }
}

// D5: both transcriptions against the DML's MathML (mathml_commands.log, pasted %.17g).
TEST(F16Control, ReferenceMatchesTheMathMlEvaluation) {
  struct Frame {
    std::string_view name;
    ref::Inputs in;
    std::array<double, 4> mathml;  // el, ail, rdr (deg), PWR (percent)
  };
  ref::Inputs base;
  base.throttle = 0.0;
  base.long_stk = 0.0;
  base.lat_stk = 0.0;
  base.pedal = 0.0;
  base.sas_on = 1.0;
  base.ap_on = 1.0;
  base.keas_cmd = 287.98158203;
  base.alt_cmd = 10013.0;
  base.lat_offset = 0.0;
  base.base_chi_cmd = 45.0;
  base.alt_msl = 10013.0;
  base.v_equiv = 287.98158203;
  base.alpha = 2.6389;
  base.beta = 0.0;
  base.phi = 0.0;
  base.theta = 2.6389;
  base.psi = 45.0;
  base.pb = 5.794e-7;
  base.qb = -2.7048e-5;
  base.rb = -1.3863e-5;
  base.throttle_trim = 0.13873817846824657;
  base.long_stk_trim = 0.1293103079;

  std::vector<Frame> frames;
  const auto add = [&](std::string_view name, auto&& change, std::array<double, 4> mathml) {
    ref::Inputs in = base;
    change(in);
    frames.push_back({name, in, mathml});
  };
  const auto none = [](ref::Inputs&) {};
  add("engagement", none,
      {-4.4581119178028228, -0.00011890848111987066, -0.00036152298881013124, 14.150429893048974});
  add("13.1 step", [](ref::Inputs& in) { in.alt_cmd = 10113.0; },
      {-25.0, -0.00011890848111987066, -0.00036152298881013124, 100.0});
  add("13.2 step", [](ref::Inputs& in) { in.keas_cmd = 282.98158203; },
      {-12.334246196614574, -0.00011890848111987066, -0.00036152298881013124, 0.0});
  add("13.3 step", [](ref::Inputs& in) { in.base_chi_cmd = 60.0; },
      {-4.4581119178028228, -21.5, 29.827999999999999, 14.150429893048974});
  add("13.4 step", [](ref::Inputs& in) { in.lat_offset = -1996.67; },
      {-4.4581119178028228, -21.5, 29.827999999999999, 14.150429893048974});
  add("disengaged",
      [](ref::Inputs& in) {
        in.sas_on = 0.0;
        in.ap_on = 0.0;
      },
      {-3.2327576975000003, -0.0, -0.0, 13.873817846824657});
  add("SAS only, pilot inputs",
      [](ref::Inputs& in) {
        in.ap_on = 0.0;
        in.throttle = 0.3;
        in.long_stk = 0.2;
        in.lat_stk = -0.4;
        in.pedal = 0.1;
        in.phi = 10.0;
        in.beta = 2.0;
        in.pb = 0.1;
        in.rb = -0.05;
        in.alpha = 4.0;
        in.theta = 3.0;
        in.v_equiv = 290.0;
      },
      {19.424904548539587, 21.5, -29.827999999999999, 0.0});
  add("SAS only, small inputs",
      [](ref::Inputs& in) {
        in.ap_on = 0.0;
        in.throttle = 0.2;
        in.long_stk = 0.05;
        in.lat_stk = -0.03;
        in.pedal = 0.02;
        in.phi = 0.05;
        in.beta = 0.02;
        in.pb = 0.002;
        in.rb = -0.001;
        in.v_equiv = 287.9;
      },
      {-5.8516787151332075, 4.151129463450137, -1.8684182678665053, 25.061357398682304});
  add("small lateral disturbance",
      [](ref::Inputs& in) {
        in.phi = 0.04;
        in.beta = 0.015;
        in.pb = 0.001;
        in.rb = -0.0005;
      },
      {-4.4581119178028228, 12.675904944529888, -4.0816572324610387, 14.150429893048974});
  add("track-error wrap",
      [](ref::Inputs& in) {
        in.psi = -170.0;
        in.base_chi_cmd = 170.0;
        in.beta = 1.0;
      },
      {-4.4581119178028228, 21.5, -29.827999999999999, 14.150429893048974});
  add("pilot inputs past their limits",
      [](ref::Inputs& in) {
        in.sas_on = 0.0;
        in.ap_on = 0.0;
        in.throttle = 1.3;
        in.long_stk = -1.4;
        in.lat_stk = 1.2;
        in.pedal = -1.5;
      },
      {21.767242302500001, -21.5, 29.827999999999999, 100.0});
  add("maneuvering, throttle at 0",
      [](ref::Inputs& in) {
        in.v_equiv = 300.0;
        in.phi = 20.0;
        in.theta = 5.0;
        in.alpha = 6.0;
        in.beta = -3.0;
        in.pb = 0.5;
        in.qb = 0.1;
        in.rb = -0.2;
        in.alt_msl = 9900.0;
        in.lat_offset = 1500.0;
        in.base_chi_cmd = 30.0;
        in.psi = 50.0;
      },
      {-25.0, 21.5, -29.827999999999999, 0.0});
  ASSERT_EQ(frames.size(), 12U);

  for (const Frame& f : frames) {
    // The reference, in the DML's units.
    const ref::Outputs r = ref::evaluate(f.in);
    EXPECT_TRUE(within(r.el, f.mathml[0], 25.0, "reference el")) << f.name;
    EXPECT_TRUE(within(r.ail, f.mathml[1], 21.5, "reference ail")) << f.name;
    EXPECT_TRUE(within(r.rdr, f.mathml[2], 30.172, "reference rdr")) << f.name;
    EXPECT_TRUE(within(r.pwr, f.mathml[3], 100.0, "reference PWR")) << f.name;

    // The SI law, its inputs converted through core/util/units.
    law::Inputs in;
    in.altitude = units::feet_to_meters(f.in.alt_msl);
    in.eas = units::knots_to_meters_per_second(f.in.v_equiv);
    in.alpha = units::degrees_to_radians(f.in.alpha);
    in.beta = units::degrees_to_radians(f.in.beta);
    in.phi = units::degrees_to_radians(f.in.phi);
    in.theta = units::degrees_to_radians(f.in.theta);
    in.psi = units::degrees_to_radians(f.in.psi);
    in.p = f.in.pb;
    in.q = f.in.qb;
    in.r = f.in.rb;
    law::Commands cmd;
    cmd.altitude = units::feet_to_meters(f.in.alt_cmd);
    cmd.eas = units::knots_to_meters_per_second(f.in.keas_cmd);
    cmd.course = units::degrees_to_radians(f.in.base_chi_cmd);
    cmd.lateral_deviation_error = units::feet_to_meters(f.in.lat_offset);
    const law::Modes modes{f.in.sas_on > 0.5, f.in.ap_on > 0.5, f.in.throttle,
                           f.in.long_stk,     f.in.lat_stk,     f.in.pedal};
    const law::Outputs o =
        law::evaluate(in, cmd, law::Trim{f.in.long_stk_trim, f.in.throttle_trim}, modes);
    EXPECT_TRUE(within(o.elevator, f.mathml[0] * kDeg, kElevatorScale, "SI el")) << f.name;
    EXPECT_TRUE(within(o.aileron_lwd, f.mathml[1] * kDeg, kAileronScale, "SI ail")) << f.name;
    EXPECT_TRUE(within(o.rudder, f.mathml[2] * kDeg, kRudderScale, "SI rdr")) << f.name;
    EXPECT_TRUE(within(o.pla_percent, f.mathml[3], 100.0, "SI PWR")) << f.name;
  }
}

// R11 and R22: the law's omega_ie_b subtraction against the reference's own DCM and Earth rate.
TEST(F16Control, EarthRelativeRateSubtractsTheEarthRateInBodyAxes) {
  constexpr double kOmega = 7.292115e-5;
  for (const double lat_deg : {0.0, 36.01916667, 89.95, -89.95}) {
    for (const double psi_deg : {0.0, 45.0, 90.0, -135.0}) {
      astra_sim_to_sw_v1 f{};
      f.lat = lat_deg * kDeg;
      f.psi = psi_deg * kDeg;
      f.theta = 5.0 * kDeg;
      f.phi = -10.0 * kDeg;
      f.p = 0.01;
      f.q = -0.02;
      f.r = 0.03;
      const Eigen::Vector3d got = law::earth_relative_rate(f);
      double p = 0.0;
      double q = 0.0;
      double r = 0.0;
      ref::earth_relative_rate(f, p, q, r);
      const std::string where = std::format("lat {} deg, psi {} deg", lat_deg, psi_deg);
      EXPECT_TRUE(within(got.x(), p, kOmega, "p")) << where;
      EXPECT_TRUE(within(got.y(), q, kOmega, "q")) << where;
      EXPECT_TRUE(within(got.z(), r, kOmega, "r")) << where;
    }
  }
  // Case 11's trim: the n frame's own rate leaves the transport rate, as
  // docs/reviews/evidence/m7-pr9-residual/derive_residual.log prints it (7 significant digits).
  const Eigen::Vector3d trim = law::earth_relative_rate(packed(trim_state()));
  EXPECT_NEAR(trim.x(), 5.794325e-07, 1e-12);
  EXPECT_NEAR(trim.y(), -2.704838e-05, 1e-11);
  EXPECT_NEAR(trim.z(), -1.386315e-05, 1e-11);
  std::cout << std::format("case 11's trim, Earth-relative rate: {:.9e} {:.9e} {:.9e} rad/s\n",
                           trim.x(), trim.y(), trim.z());
}

// R11 (verification N-02): 13.4's cross-track on the course, 609.6 m left (-) and right (+) of it,
// with h0 fixed, the points placed in the initial plane by this test's own WGS-84 radii.
TEST(F16Control, CrossTrackOnLeftAndRightOfTheCourse) {
  constexpr double kA = 6378137.0;
  constexpr double kF = 1.0 / 298.257223563;
  const double e2 = kF * (2.0 - kF);
  const double s2 = std::sin(kLat0) * std::sin(kLat0);
  const double m0 = kA * (1.0 - e2) / std::pow(1.0 - e2 * s2, 1.5);
  const double n0 = kA / std::sqrt(1.0 - e2 * s2);
  const double chi = 45.0 * kDeg;
  const law::BasePoint base = law::base_point(kLat0, kLon0, kH0);
  for (const double along : {0.0, 2500.0, 10000.0}) {
    for (const double offset : {0.0, -609.6, 609.6}) {
      const double dn = along * std::cos(chi) - offset * std::sin(chi);
      const double de = along * std::sin(chi) + offset * std::cos(chi);
      const double lat = kLat0 + dn / (m0 + kH0);
      const double lon = kLon0 + de / ((n0 + kH0) * std::cos(kLat0));
      const double d = law::cross_track(base, chi, lat, lon);
      EXPECT_TRUE(within(d, offset, 609.6, "cross-track"))
          << std::format("{} m along, {} m right", along, offset);
      if (offset != 0.0) {
        EXPECT_EQ(d > 0.0, offset > 0.0) << "positive is right of the course";
      }
    }
  }
}

// R12 (the plan's Q3): EAS from tas, cas and mach against tas sqrt(rho / rho0), rho from the same
// AtmoState the frame is packed from.
TEST(F16Control, EquivalentAirspeedEqualsTasTimesRootDensityRatio) {
  const double rho0 = astra::icd_pack::kSeaLevelPressure * astra::icd_pack::kMeanMolecularWeight /
                      (astra::icd_pack::kGasConstant * astra::icd_pack::kSeaLevelTemperature);
  double worst = 0.0;
  int frames = 0;
  const auto check = [&](const astra::StateVector& y, std::string_view where) {
    astra::EnvironmentSample env;
    const astra_sim_to_sw_v1 f = packed(y, &env);
    ASSERT_EQ(law::refusal_bits(f), 0U) << where;
    const double eas = law::equivalent_airspeed(f.cas, f.mach);
    const double expected = f.tas * std::sqrt(env.atmosphere.density_kg_m3 / rho0);
    EXPECT_LE(std::abs(eas - expected), kRelative * expected)
        << where << std::format(": {:.17g} against {:.17g}", eas, expected);
    worst = std::max(worst, std::abs(eas - expected) / expected);
    ++frames;
  };
  check(trim_state(), "case 11's trim");
  for (const double altitude : {0.0, 1000.0, 2900.0, kH0, 3200.0, 5000.0, 8000.0, 11000.0}) {
    for (const double mach : {0.1, 0.3, 0.45, 0.525, 0.6, 0.8, 0.95}) {
      const double sound = isa().at(altitude).speed_of_sound_m_s;
      const astra::StateVector y =
          make_state({kLat0, kLon0, altitude}, {mach * sound * 0.999, 3.0, 8.0}, {0.6, 0.05, 0.1},
                     {0.0, 0.0, 0.0});
      check(y, std::format("altitude {} m, Mach {}", altitude, mach));
    }
  }
  std::cout << std::format("R12: {} frames, largest relative difference {:.3g} (tol 1e-12)\n",
                           frames, worst);
  EXPECT_EQ(rho0, 101325.0 * 28.9644 / (8314.32 * 288.15));
}

// R12 and the plan's Q8 at the unit level: each refused condition, through the Transport API of
// the plugin as built, gives five non-finite commands and names its bits in transport.peer.refusal.
TEST(F16Control, RefusesEveryFlaggedFrame) {
  LoadedPeer peer{peer_block()};
  const astra_sim_to_sw_v1 good = packed(trim_state());
  std::uint32_t seq = 0;

  const astra_sw_to_sim_v1 first = peer.exchange(good, seq++);
  EXPECT_TRUE(std::isfinite(first.elevator) && std::isfinite(first.throttle[0]));
  EXPECT_EQ(first.throttle[0], first.throttle[1]);
  EXPECT_EQ(peer.signal("transport.peer.refusal"), 0.0);
  EXPECT_TRUE(std::isfinite(peer.signal("transport.peer.equivalent_airspeed")));
  EXPECT_NE(good.status_flags & ASTRA_ICD_STATUS_ACCEL_INVALID, 0U)
      << "frame 0 carries ACCEL_INVALID, which the law does not read and the peer does not refuse";

  struct Refused {
    std::string_view name;
    std::uint32_t flags;  // set on the frame
    double mach;          // the frame's Mach
    std::uint32_t bits;   // expected in transport.peer.refusal
  };
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  const std::array<Refused, 8> cases = {{
      {"CAS_SUPERSONIC", ASTRA_ICD_STATUS_CAS_SUPERSONIC, 1.2, ASTRA_ICD_STATUS_CAS_SUPERSONIC},
      {"ATMOSPHERE_INVALID", ASTRA_ICD_STATUS_ATMOSPHERE_INVALID, 0.0,
       ASTRA_ICD_STATUS_ATMOSPHERE_INVALID | law::kRefusalMachNotPositive},
      {"WIND_INVALID", ASTRA_ICD_STATUS_WIND_INVALID, 0.0,
       ASTRA_ICD_STATUS_WIND_INVALID | law::kRefusalMachNotPositive},
      {"LATLON_NOT_MODELED", ASTRA_ICD_STATUS_LATLON_NOT_MODELED, good.mach,
       ASTRA_ICD_STATUS_LATLON_NOT_MODELED},
      {"Mach 0", 0U, 0.0, law::kRefusalMachNotPositive},
      {"Mach negative", 0U, -0.1, law::kRefusalMachNotPositive},
      {"Mach NaN", 0U, kNan, law::kRefusalMachNotPositive},
      {"two flags", ASTRA_ICD_STATUS_CAS_SUPERSONIC | ASTRA_ICD_STATUS_LATLON_NOT_MODELED, 1.1,
       ASTRA_ICD_STATUS_CAS_SUPERSONIC | ASTRA_ICD_STATUS_LATLON_NOT_MODELED},
  }};
  for (const Refused& c : cases) {
    astra_sim_to_sw_v1 frame = good;
    frame.frame = 10ULL * seq;
    frame.status_flags |= c.flags;
    frame.mach = c.mach;
    const astra_sw_to_sim_v1 reply = peer.exchange(frame, seq++);
    EXPECT_TRUE(all_non_finite(reply)) << c.name;
    EXPECT_EQ(reply.magic, ASTRA_ICD_MAGIC) << c.name;
    EXPECT_EQ(reply.frame, frame.frame) << c.name;
    EXPECT_EQ(static_cast<std::uint32_t>(peer.signal("transport.peer.refusal")), c.bits) << c.name;
    EXPECT_TRUE(std::isnan(peer.signal("transport.peer.equivalent_airspeed"))) << c.name;
  }
  // ACCEL_INVALID alone is not refused, and the next good frame computes again.
  astra_sim_to_sw_v1 frame = good;
  frame.frame = 10ULL * seq;
  const astra_sw_to_sim_v1 again = peer.exchange(frame, seq++);
  EXPECT_FALSE(all_non_finite(again));
  EXPECT_EQ(again.elevator, first.elevator);
  EXPECT_EQ(peer.signal("transport.peer.refusal"), 0.0);

  // 13.4's base point (ADR-0029 Decisions 2 and 6): with the lateral offset configured, a first
  // exchange whose frame is not 0 is refused, and nothing ever latches...
  const std::string side_step =
      peer_block() + "  lateral_offset: {frame: 20000, offset_ft: 2000}\n";
  {
    LoadedPeer late{side_step};
    astra_sim_to_sw_v1 f = good;
    f.frame = 10;
    EXPECT_TRUE(all_non_finite(late.exchange(f, 1)));
    EXPECT_EQ(static_cast<std::uint32_t>(late.signal("transport.peer.refusal")),
              law::kRefusalBasePointFrame);
    f.frame = 20;
    EXPECT_TRUE(all_non_finite(late.exchange(f, 2))) << "no base point ever latched";
  }
  // ...and a frame 0 refused for LATLON_NOT_MODELED latches nothing, so every later frame is
  // refused for the base point.
  {
    LoadedPeer flat{side_step};
    astra_sim_to_sw_v1 f = good;
    f.status_flags |= ASTRA_ICD_STATUS_LATLON_NOT_MODELED;
    EXPECT_TRUE(all_non_finite(flat.exchange(f, 0)));
    f = good;
    f.frame = 10;
    EXPECT_TRUE(all_non_finite(flat.exchange(f, 1)));
    EXPECT_EQ(static_cast<std::uint32_t>(flat.signal("transport.peer.refusal")),
              law::kRefusalBasePointFrame);
  }
  // A good frame 0 latches, and later frames compute.
  {
    LoadedPeer latched{side_step};
    EXPECT_FALSE(all_non_finite(latched.exchange(good, 0)));
    astra_sim_to_sw_v1 f = good;
    f.frame = 10;
    EXPECT_FALSE(all_non_finite(latched.exchange(f, 1)));
    EXPECT_EQ(latched.signal("transport.peer.refusal"), 0.0);
  }
}

// R12 and Q8 through Simulation: a refused frame 0 halts kTransport at the exchange's non-finite
// check, naming astra_sw_to_sim_v1.elevator, before any physics halt. transport.peer.refusal is
// read through the registry pointer after the halt, since no log sample exists.
TEST(F16Control, FlaggedFramesHaltTheExchangeNamingTheField) {
  const std::string case11 = read_text(config_path("case11_trim_check_subsonic.yaml"));
  const std::string case12 = read_text(config_path("case12_trim_check_supersonic.yaml"));
  const auto replaced = [](std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos || text.find(from, at + 1) != std::string::npos) {
      throw astra::Error("the fixture text must hold exactly one '" + std::string{from} + "'");
    }
    text.replace(at, from.size(), to);
    return text;
  };
  struct Halting {
    std::string_view name;
    std::string config;
    std::uint32_t bits;
  };
  const std::string flat =
      replaced(replaced(replaced(case11, "earth: plugins/physical/earth/wgs84_rotating",
                                 "earth: plugins/physical/earth/flat"),
                        "gravity: plugins/physical/gravity/wgs84_j2",
                        "gravity: plugins/physical/gravity/constant"),
               "p_n: [0.6286530522161018, -1.3207682150955165, 3051.9624000000003]",
               "p_n: [0.0, 0.0, -3051.9624000000003]");
  const std::vector<Halting> cases = {
      {"CAS_SUPERSONIC (case 12's IC)", case12 + transport_block(),
       ASTRA_ICD_STATUS_CAS_SUPERSONIC},
      {"LATLON_NOT_MODELED (earth/flat)", flat + transport_block(),
       ASTRA_ICD_STATUS_LATLON_NOT_MODELED},
      {"ATMOSPHERE_INVALID (tests/plugins/nan_atmosphere)",
       replaced(case11, "atmosphere: plugins/physical/atmosphere/isa1976",
                "atmosphere: tests/plugins/nan_atmosphere") +
           transport_block(),
       ASTRA_ICD_STATUS_ATMOSPHERE_INVALID | law::kRefusalMachNotPositive},
      {"WIND_INVALID (tests/plugins/nan_wind)",
       replaced(case11, "  atmosphere: plugins/physical/atmosphere/isa1976\n",
                "  atmosphere: plugins/physical/atmosphere/isa1976\n"
                "  wind: {plugin: tests/plugins/nan_wind}\n") +
           transport_block(),
       ASTRA_ICD_STATUS_WIND_INVALID | law::kRefusalMachNotPositive},
  };
  for (const Halting& c : cases) {
    astra::Simulation sim = astra::Simulation::from_config(
        astra::Config::from_string(c.config, std::string{c.name}), ASTRA_TEST_PLUGIN_DIR);
    const double* refusal = sim.registry().find("transport.peer.refusal");
    const double* fault = sim.registry().find("transport.fault");
    ASSERT_NE(refusal, nullptr) << c.name;
    ASSERT_NE(fault, nullptr) << c.name;
    const astra::HaltReason halt = sim.run(1);
    EXPECT_EQ(halt.check, astra::HealthCheck::kTransport) << c.name;
    EXPECT_EQ(halt.source, "astra_sw_to_sim_v1.elevator") << c.name;
    EXPECT_EQ(halt.frame, 0U) << c.name;
    EXPECT_EQ(*fault, static_cast<double>(astra::TransportFault::kNonFiniteCommand)) << c.name;
    EXPECT_EQ(static_cast<std::uint32_t>(*refusal), c.bits) << c.name;
    std::cout << std::format("{}: halted check {} at frame {} naming {}, refusal 0x{:x}\n", c.name,
                             static_cast<int>(halt.check), halt.frame, halt.source,
                             static_cast<std::uint32_t>(*refusal));
  }
}

// R23: the law with SAS and autopilot off, fed the inverse limit-and-scale of the trim's controls,
// returns the trim's own elevator and throttle within 1e-12, with no aileron or rudder.
TEST(F16Control, DisengagedLawReproducesTheTrim) {
  const law::Modes off{false, false, 0.0, 0.0, 0.0, 0.0};
  const law::Inputs engaged_inputs = law::inputs_from(packed(trim_state()));
  const law::Commands cmd{units::feet_to_meters(10113.0), 140.0, 1.0, 300.0};
  const auto check = [&](double elevator, double throttle, std::string_view where) {
    const law::IcdCommands icd = law::to_icd(
        law::evaluate(engaged_inputs, cmd, law::trim_from_controls(elevator, throttle), off));
    EXPECT_LE(std::abs(icd.elevator - elevator), 1e-12) << where;
    EXPECT_LE(std::abs(icd.throttle - throttle), 1e-12) << where;
    EXPECT_EQ(icd.aileron, 0.0) << where;
    EXPECT_EQ(icd.rudder, 0.0) << where;
  };
  check(kTrimElevator, kTrimThrottle, "case 11's and 13.x's trim");
  for (int el_deg = -25; el_deg <= 25; el_deg += 5) {
    for (const double throttle : {0.0, 0.25, 0.5, 0.75, 1.0}) {
      check(el_deg * kDeg, throttle, std::format("{} deg, throttle {}", el_deg, throttle));
    }
  }
  // The peer applies the same inverse at init.
  astra::aircraft_f16_nesc_control::NescControlPeer peer;
  astra::SignalRegistry registry;
  peer.init(astra::Config::from_string(peer_block(), "f16-control-trim"), registry);
  const law::Trim trim = law::trim_from_controls(kTrimElevator, kTrimThrottle);
  EXPECT_EQ(peer.trim().long_stick, trim.long_stick);
  EXPECT_EQ(peer.trim().throttle, trim.throttle);
  std::cout << std::format("trimmedPilotControl_long {:.17g}, _throttle {:.17g}\n", trim.long_stick,
                           trim.throttle);
}

// The M7 plan's Q11 as amended 2026-10-13 after the measurement: the peer's LQR references are the
// trim's pitch (trimmedAlpha = trimmedTheta = peer.trim.theta) and the initial equivalent-airspeed
// command (trimmedKEAS), bit for bit, in the fixture and in each tracked config; the reported
// variant's are the DML's hard-coded ones.
TEST(F16Control, LqrReferencesAreTheTrimsPitchAndTheInitialAirspeedCommand) {
  const auto expect_references = [](const law::References& got, double theta, double eas,
                                    std::string_view where) {
    EXPECT_TRUE(std::bit_cast<std::uint64_t>(got.alpha) == std::bit_cast<std::uint64_t>(theta))
        << where << std::format(": trimmedAlpha {:.17g} against {:.17g}", got.alpha, theta);
    EXPECT_TRUE(std::bit_cast<std::uint64_t>(got.theta) == std::bit_cast<std::uint64_t>(theta))
        << where << std::format(": trimmedTheta {:.17g} against {:.17g}", got.theta, theta);
    EXPECT_TRUE(std::bit_cast<std::uint64_t>(got.eas) == std::bit_cast<std::uint64_t>(eas))
        << where << std::format(": trimmedKEAS {:.17g} against {:.17g} m/s", got.eas, eas);
  };
  {
    astra::aircraft_f16_nesc_control::NescControlPeer peer;
    astra::SignalRegistry registry;
    peer.init(astra::Config::from_string(peer_block(), "f16-control-references"), registry);
    expect_references(peer.references(), kTrimEuler[1],
                      units::knots_to_meters_per_second(287.98158203), "the fixture");
  }
  for (const std::string_view name :
       {"case13p1_subsonic_altitude_change.yaml", "case13p2_subsonic_airspeed_change.yaml",
        "case13p3_subsonic_heading_change.yaml", "case13p4_subsonic_lateral_side_step.yaml"}) {
    const astra::Config config = astra::Config::from_file(config_path(name));
    astra::aircraft_f16_nesc_control::NescControlPeer peer;
    astra::SignalRegistry registry;
    peer.init(config.at("transport"), registry);
    expect_references(peer.references(), config.at("initial.state.euler_nb").at(1).number(),
                      units::knots_to_meters_per_second(
                          config.number("transport.peer.commands.equivalent_airspeed_knots")),
                      name);
  }
  // Q11's reported variant.
  astra::aircraft_f16_nesc_control::Options dml;
  dml.dml_references = true;
  astra::aircraft_f16_nesc_control::NescControlPeer peer{dml};
  astra::SignalRegistry registry;
  peer.init(astra::Config::from_string(peer_block(), "f16-control-dml-references"), registry);
  // law::References{} holds the DML's values
  // (GainsAndLimitsEqualTheDmlValuesTimesTheirConversions).
  expect_references(peer.references(), law::References{}.theta, law::References{}.eas,
                    "the DML's hard-coded references");
  EXPECT_EQ(law::References{}.alpha, law::References{}.theta);
  std::cout << std::format(
      "decided references: trimmedAlpha = trimmedTheta = {:.17g} rad, "
      "trimmedKEAS {:.17g} m/s\n",
      kTrimEuler[1], units::knots_to_meters_per_second(287.98158203));
}

TEST(F16Control, InitRefusesMissingOrOutOfRangeKeys) {
  struct Bad {
    std::string block;
    std::string_view key;
  };
  const std::vector<Bad> cases = {
      {"{}\n", "peer"},
      {"peer:\n  commands: {altitude_ft: 10013, equivalent_airspeed_knots: 288, course_deg: 45}\n",
       "peer.trim"},
      {"peer:\n  trim: {throttle: 0.1, theta: 0.05}\n"
       "  commands: {altitude_ft: 10013, equivalent_airspeed_knots: 288, course_deg: 45}\n",
       "peer.trim.elevator"},
      {"peer:\n  trim: {elevator: -0.05, throttle: 0.1}\n"
       "  commands: {altitude_ft: 10013, equivalent_airspeed_knots: 288, course_deg: 45}\n",
       "peer.trim.theta"},
      {peer_block(kTrimElevator, kTrimThrottle, 288.0, std::numeric_limits<double>::quiet_NaN()),
       "peer.trim.theta"},
      {peer_block(kTrimElevator, kTrimThrottle, 288.0, 1.6), "peer.trim.theta"},
      {peer_block(0.5), "peer.trim.elevator"},
      {peer_block(kTrimElevator, 1.2), "peer.trim.throttle"},
      {peer_block(kTrimElevator, -0.1), "peer.trim.throttle"},
      {peer_block(kTrimElevator, kTrimThrottle, 0.0), "peer.commands.equivalent_airspeed_knots"},
      {peer_block(kTrimElevator, kTrimThrottle, -5.0), "peer.commands.equivalent_airspeed_knots"},
      {"peer:\n  trim: {elevator: -0.05, throttle: 0.1, theta: 0.05}\n"
       "  commands: {altitude_ft: .nan, equivalent_airspeed_knots: 288, course_deg: 45}\n",
       "peer.commands.altitude_ft"},
      {"peer:\n  trim: {elevator: -0.05, throttle: 0.1, theta: 0.05}\n"
       "  commands: {altitude_ft: 10013, equivalent_airspeed_knots: 288}\n",
       "peer.commands.course_deg"},
  };
  for (const Bad& c : cases) {
    astra::aircraft_f16_nesc_control::NescControlPeer peer;
    astra::SignalRegistry registry;
    try {
      peer.init(astra::Config::from_string(c.block, "f16-control-bad"), registry);
      ADD_FAILURE() << "accepted: " << c.block;
    } catch (const astra::ConfigError& error) {
      EXPECT_EQ(error.config_path(), c.key) << c.block << " -> " << error.what();
    }
  }
}

namespace {

// --- R10 and R22: the schedules and the onset --------------------------------------------------

// This file's own initial-plane cross-track (ADR-0029 Decision 6), from its own WGS-84 radii.
double reference_cross_track(double lat0, double lon0, double h0, double chi, double lat,
                             double lon) {
  constexpr double kA = 6378137.0;
  constexpr double kF = 1.0 / 298.257223563;
  const double e2 = kF * (2.0 - kF);
  const double s2 = std::sin(lat0) * std::sin(lat0);
  const double m0 = kA * (1.0 - e2) / std::pow(1.0 - e2 * s2, 1.5);
  const double n0 = kA / std::sqrt(1.0 - e2 * s2);
  const double dn = (lat - lat0) * (m0 + h0);
  const double de = (lon - lon0) * (n0 + h0) * std::cos(lat0);
  return (-dn * std::sin(chi)) + (de * std::cos(chi));
}

// One 13.x case's commands, in Vol II's units (Tables 37 to 40; D.1.14 for the airspeed), and the
// peer lines that schedule them. The step frames are Vol II's times over dt = 1 ms.
struct StepCase {
  std::string_view name;
  std::string_view peer_lines;
  std::uint64_t step_frame;
  std::uint64_t last_frame;
  double alt_before_ft;
  double alt_after_ft;
  double keas_before;
  double keas_after;
  double course_before_deg;
  double course_after_deg;
  bool side_step;  // 13.4: a 2,000 ft lateral offset from the step on
};

constexpr double kKeas0 = 287.98158203;  // peer_block's initial command literal
// The tracked 13.x configs, in kStepCases' order.
constexpr std::array<std::string_view, 4> kTrackedConfigs = {
    "case13p1_subsonic_altitude_change.yaml", "case13p2_subsonic_airspeed_change.yaml",
    "case13p3_subsonic_heading_change.yaml", "case13p4_subsonic_lateral_side_step.yaml"};
const std::array<StepCase, 4> kStepCases = {{
    {"13.1", "  schedule:\n    - {frame: 5000, altitude_ft: 10113}\n", 5000, 20000, 10013.0,
     10113.0, kKeas0, kKeas0, 45.0, 45.0, false},
    {"13.2", "  schedule:\n    - {frame: 5000, equivalent_airspeed_knots: 282.98158203}\n", 5000,
     20000, 10013.0, 10013.0, kKeas0, kKeas0 - 5.0, 45.0, 45.0, false},
    {"13.3", "  schedule:\n    - {frame: 15000, course_deg: 60}\n", 15000, 30000, 10013.0, 10013.0,
     kKeas0, kKeas0, 45.0, 60.0, false},
    {"13.4", "  lateral_offset: {frame: 20000, offset_ft: 2000}\n", 20000, 60000, 10013.0, 10013.0,
     kKeas0, kKeas0, 45.0, 45.0, true},
}};

struct Onset {
  bool reply_matches = false;
  bool latched_matches = false;
  bool changed = false;  // the reply differs from the previous exchange's
  double ratio = 0.0;    // the largest |reply - reference| / bound
};

bool same_bits(double a, double b) {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

// Runs `sim` through `frame` and checks that frame's exchange (R22): the reply to the recorded
// request against an independent evaluation of the DML at that frame's commands, within R11's
// seeded bound; the latched controls bit for bit equal to the reply (zero compute delay).
Onset onset_at(astra::Simulation& sim, const StepCase& sc, std::uint64_t frame) {
  const std::uint64_t now = sim.clock().frame();
  EXPECT_FALSE(sim.run(frame + 1 - now)) << sc.name << ": halted before frame " << frame;
  const astra::ExchangeRecorder* recorder = sim.exchange_recorder();
  const auto requests = recorder->requests();
  const auto replies = recorder->replies();
  const astra_sim_to_sw_v1& request = requests.back();
  const astra_sw_to_sim_v1& reply = replies.back();
  EXPECT_EQ(request.frame, frame) << sc.name;
  EXPECT_EQ(reply.frame, frame) << sc.name;

  const bool after = frame >= sc.step_frame;
  Case c;
  c.frame = request;
  c.alt_cmd_ft = after ? sc.alt_after_ft : sc.alt_before_ft;
  c.keas_cmd = after ? sc.keas_after : sc.keas_before;
  c.course_deg = after ? sc.course_after_deg : sc.course_before_deg;
  if (sc.side_step && after) {
    const astra_sim_to_sw_v1& f0 = requests.front();
    const double d =
        reference_cross_track(f0.lat, f0.lon, f0.altitude, 45.0 * kDeg, request.lat, request.lon);
    c.lat_offset_ft = (d - (2000.0 * kFt)) / kFt;
  }
  c.long_stk_trim = law::trim_from_controls(kTrimElevator, kTrimThrottle).long_stick;
  c.throttle_trim = kTrimThrottle;
  // The peer's decided LQR references (Q11 as amended): the trim's pitch, and the initial command.
  c.refs = law::References{kTrimEuler[1], kTrimEuler[1], units::knots_to_meters_per_second(kKeas0)};
  const ref::Inputs in = reference_inputs(c);
  const ref::Outputs want = ref::evaluate(in);
  const ref::Icd icd = ref::to_icd(want);
  const std::array<double, 4> bound = bounds(icd, conditioning(c, in, want, gains_at(in, want)));
  const std::array<double, 4> got{reply.elevator, reply.aileron, reply.rudder, reply.throttle[0]};
  const std::array<double, 4> expected{icd.elevator, icd.aileron, icd.rudder, icd.throttle};

  Onset out;
  out.reply_matches = same_bits(reply.throttle[0], reply.throttle[1]);
  for (std::size_t k = 0; k < 4; ++k) {
    const double ratio = std::abs(got.at(k) - expected.at(k)) / bound.at(k);
    out.ratio = std::max(out.ratio, ratio);
    out.reply_matches = out.reply_matches && ratio <= 1.0;
  }
  const auto latched = [&sim](std::string_view name) {
    const double* value = sim.registry().find(name);
    return value == nullptr ? std::numeric_limits<double>::quiet_NaN() : *value;
  };
  out.latched_matches = same_bits(latched("v0.controls.elevator"), reply.elevator) &&
                        same_bits(latched("v0.controls.aileron"), reply.aileron) &&
                        same_bits(latched("v0.controls.rudder"), reply.rudder) &&
                        same_bits(latched("v0.controls.throttle"), reply.throttle[0]);
  if (replies.size() >= 2) {
    const astra_sw_to_sim_v1& before = replies[replies.size() - 2];
    out.changed = !same_bits(before.elevator, reply.elevator) ||
                  !same_bits(before.aileron, reply.aileron) ||
                  !same_bits(before.rudder, reply.rudder) ||
                  !same_bits(before.throttle[0], reply.throttle[0]);
  }
  std::cout << std::format(
      "{} frame {}: reply el {:.9f} ail {:.9f} rdr {:.9f} rad, throttle {:.9f}; largest |d| / "
      "bound {:.3g}; latched {}\n",
      sc.name, frame, reply.elevator, reply.aileron, reply.rudder, reply.throttle[0], out.ratio,
      out.latched_matches ? "bit for bit" : "DIFFERS");
  return out;
}

astra::Simulation onset_sim(const StepCase& sc, std::string_view plugin,
                            std::string_view variant_lines = "") {
  const std::string text =
      read_text(config_path("case11_trim_check_subsonic.yaml")) +
      transport_block(kTrimElevator, kTrimThrottle,
                      std::string{sc.peer_lines} + std::string{variant_lines}, plugin);
  return astra::Simulation::from_config(astra::Config::from_string(text, std::string{sc.name}),
                                        ASTRA_TEST_PLUGIN_DIR);
}

// R10: one peer block's command timeline, read from the peer's signals at every exchange, against
// Vol II's table for its case.
void expect_timeline(const StepCase& sc, LoadedPeer& peer, std::string_view where) {
  astra_sim_to_sw_v1 frame = packed(trim_state());
  double keas0 = 0.0;
  for (std::uint64_t f = 0; f <= sc.last_frame; f += 10) {
    frame.frame = f;
    static_cast<void>(peer.exchange(frame, static_cast<std::uint32_t>(f / 10)));
    const bool after = f >= sc.step_frame;
    const double alt_ft = peer.signal("transport.peer.altitude_command") / kFt;
    const double keas = peer.signal("transport.peer.equivalent_airspeed_command") / kKt;
    const double course = peer.signal("transport.peer.course_command") / kDeg;
    const double offset = peer.signal("transport.peer.lateral_deviation_error");
    const std::string at = std::format("{} {} frame {}", where, sc.name, f);
    ASSERT_NEAR(alt_ft, after ? sc.alt_after_ft : sc.alt_before_ft, 1e-9) << at;
    // Vol II gives the initial airspeed command as 287.98 KEAS (D.1.14) and 13.2's step as 5 kt.
    if (f == 0) {
      keas0 = keas;
      ASSERT_NEAR(keas0, 287.98, 0.005) << at;
    }
    ASSERT_NEAR(keas - keas0, after ? sc.keas_after - sc.keas_before : 0.0, 1e-9) << at;
    ASSERT_NEAR(course, after ? sc.course_after_deg : sc.course_before_deg, 1e-12) << at;
    // At the base point itself d = 0, so the error is 0 before the step and -2,000 ft from it.
    ASSERT_NEAR(offset, sc.side_step && after ? -2000.0 * kFt : 0.0, 1e-9) << at;
  }
}

}  // namespace

// R10 (the peer's half; the port's is PR 12's): each case's schedule, frame for frame, against
// Vol II Tables 37 to 40.
TEST(F16Control, ScheduleMatchesVolIITables) {
  for (const StepCase& sc : kStepCases) {
    LoadedPeer fixture{peer_block() + std::string{sc.peer_lines}};
    expect_timeline(sc, fixture, "fixture");
  }
  // The tracked configs (aircraft/f16/configs/case13p*.yaml).
  for (std::size_t i = 0; i < kStepCases.size(); ++i) {
    const astra::Config config = astra::Config::from_file(config_path(kTrackedConfigs.at(i)));
    LoadedPeer tracked{config.at("transport")};
    expect_timeline(kStepCases.at(i), tracked, kTrackedConfigs.at(i));
  }
}

// R22: at engagement (frame 0) and at each case's command step, the reply to the recorded request
// is the DML's command for that frame, computed independently, and it is latched the same frame.
// Frame 5000, 15000 or 20000 is an exchange frame at divisor 10. 13.1's and 13.2's onset at 5 s is
// inside the ungated span, which only these fixtures check.
TEST(F16Control, OnsetAtEngagementAndAtEachCommandStep) {
  for (const StepCase& sc : kStepCases) {
    astra::Simulation sim = onset_sim(sc, kPeer);
    for (const std::uint64_t frame : {std::uint64_t{0}, sc.step_frame - 10, sc.step_frame}) {
      const Onset o = onset_at(sim, sc, frame);
      EXPECT_TRUE(o.reply_matches) << sc.name << " frame " << frame;
      EXPECT_TRUE(o.latched_matches) << sc.name << " frame " << frame;
      if (frame == sc.step_frame) {
        EXPECT_TRUE(o.changed) << sc.name << ": the step must change the command";
      }
    }
  }
}

// R22's plant: a peer that applies each command one exchange late fails the onset fixture at every
// step (tests/plugins/nesc_control_variant's one_exchange_delay; at engagement it has nothing
// earlier to send, so frame 0 still matches).
TEST(F16Control, APlantedOneExchangeDelayFailsTheOnsetFixture) {
  for (const StepCase& sc : kStepCases) {
    astra::Simulation sim =
        onset_sim(sc, "tests/plugins/nesc_control_variant", "  test_variant: one_exchange_delay\n");
    const Onset engagement = onset_at(sim, sc, 0);
    EXPECT_TRUE(engagement.reply_matches) << sc.name;
    const Onset step = onset_at(sim, sc, sc.step_frame);
    EXPECT_FALSE(step.reply_matches) << sc.name << ": the delayed reply must fail the fixture";
    EXPECT_TRUE(step.latched_matches) << sc.name << ": the delay is the peer's, not the exchange's";
  }
}

namespace {

// One law evaluation at frame-like inputs that drive the totals where `rate_r` and `delta_theta`
// put them (a large r saturates totLatStk and totPedal on the same side; a large deltaTheta,
// totLongStk), engaged, from case 11's trim.
law::IcdCommands saturated(double rate_r, double theta_offset, double eas_offset) {
  law::Inputs in = law::inputs_from(packed(trim_state()));
  in.r = rate_r;
  in.theta += theta_offset;
  in.eas += eas_offset;
  law::Commands cmd;
  cmd.altitude = in.altitude;
  cmd.eas = in.eas - eas_offset;
  cmd.course = in.beta + in.psi;
  return law::to_icd(law::evaluate(in, cmd, law::trim_from_controls(kTrimElevator, kTrimThrottle)));
}

double limit_at(const astra::Config& config, std::string_view key, std::size_t end) {
  return config.at("transport.limits").at(key).at(end).number();
}

bool bit_equal(double a, double b) {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

}  // namespace

// The plan's D3: each tracked config carries one trim result, the peer's copy bit for bit equal to
// the vehicle's initial controls, and (Q11 as amended 2026-10-13) its theta bit for bit equal to
// the initial state's pitch, euler_nb[1].
TEST(F16Control, EachConfigsPeerTrimEqualsItsInitialControls) {
  for (const std::string_view name : kTrackedConfigs) {
    const astra::Config config = astra::Config::from_file(config_path(name));
    const double theta = config.number("transport.peer.trim.theta");
    const double pitch = config.at("initial.state.euler_nb").at(1).number();
    EXPECT_TRUE(bit_equal(theta, pitch))
        << name << ": transport.peer.trim.theta" << std::format(" {:.17g}", theta)
        << " against initial.state.euler_nb[1]" << std::format(" {:.17g}", pitch);
    for (const std::string_view key : {"elevator", "throttle"}) {
      const double peer = config.number("transport.peer.trim." + std::string{key});
      const double vehicle = config.number("vehicle.initial_controls.controls." + std::string{key});
      EXPECT_TRUE(bit_equal(peer, vehicle))
          << name << ": transport.peer.trim." << key << std::format(" {:.17g}", peer)
          << " against the initial control" << std::format(" {:.17g}", vehicle);
    }
  }
}

// The plan's D9 (amendment 3, with its 2026-10-13 wording fix): each tracked config's exchange
// limits, read through Config, are bit for bit the law's own saturated values, and those values
// pass the exchange's inclusive lo <= v <= hi check (lockstep_exchange.cpp), so a saturated
// command is never halted. 13.1's elevator at its step, exactly -25 deg, is the case this
// protects.
TEST(F16Control, ExchangeLimitsContainTheLawsRange) {
  // The law's saturated outputs, each way.
  const law::IcdCommands up = saturated(10.0, -1.0, -50.0);   // r > 0, theta low, EAS low
  const law::IcdCommands down = saturated(-10.0, 1.0, 50.0);  // r < 0, theta high, EAS high
  EXPECT_TRUE(bit_equal(up.elevator, -down.elevator));
  EXPECT_TRUE(bit_equal(std::abs(up.elevator), law::kElevatorRange));
  EXPECT_TRUE(bit_equal(std::abs(up.aileron), law::kAileronRange));
  EXPECT_TRUE(bit_equal(std::abs(up.rudder), law::kRudderRange))
      << std::format("{:.17g} against {:.17g}", up.rudder, law::kRudderRange);
  EXPECT_TRUE(bit_equal(std::abs(down.rudder), law::kRudderRange));
  EXPECT_TRUE((up.throttle == 1.0 && down.throttle == 0.0) ||
              (up.throttle == 0.0 && down.throttle == 1.0));

  // 13.1's step: deltaThetaCmd lands on +5 deg and the stick saturates, so the elevator is exactly
  // -25 deg.
  Case step = engaged_trim_case();
  step.alt_cmd_ft = 10113.0;
  const law::IcdCommands at_step = si_commands(step);
  EXPECT_TRUE(bit_equal(at_step.elevator, -law::kElevatorRange));
  EXPECT_TRUE(bit_equal(at_step.throttle, 1.0));

  for (const std::string_view name : kTrackedConfigs) {
    const astra::Config config = astra::Config::from_file(config_path(name));
    const std::array<std::pair<std::string_view, double>, 4> ranges = {
        {{"elevator", law::kElevatorRange},
         {"aileron", law::kAileronRange},
         {"rudder", law::kRudderRange},
         {"throttle", 1.0}}};
    for (const auto& [key, range] : ranges) {
      const double lo = limit_at(config, key, 0);
      const double hi = limit_at(config, key, 1);
      EXPECT_TRUE(bit_equal(hi, range)) << name << " " << key << std::format(" hi {:.17g}", hi);
      EXPECT_TRUE(bit_equal(lo, key == "throttle" ? 0.0 : -range))
          << name << " " << key << std::format(" lo {:.17g}", lo);
    }
    const auto passes = [&](std::string_view key, double v) {
      return v >= limit_at(config, key, 0) && v <= limit_at(config, key, 1);
    };
    for (const law::IcdCommands& c : {up, down, at_step}) {
      EXPECT_TRUE(passes("elevator", c.elevator)) << name;
      EXPECT_TRUE(passes("aileron", c.aileron)) << name;
      EXPECT_TRUE(passes("rudder", c.rudder)) << name;
      EXPECT_TRUE(passes("throttle", c.throttle)) << name;
    }
  }
}

// The plan's Q4 (Joe's amendment 10): the peer cannot see the Earth model, so every config in the
// tree that names it must pair it with the rotating WGS-84 Earth it assumes. The scan reads every
// *.yaml under aircraft/ and tests/ that parses as a config with a transport block.
TEST(F16Control, TrackedConfigsPairThePeerWithTheRotatingEarth) {
  const std::filesystem::path root =
      std::filesystem::path{ASTRA_F16_CONFIG_DIR}.parent_path().parent_path().parent_path();
  std::vector<std::string> found;
  for (const std::string_view top : {"aircraft", "tests"}) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / top)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".yaml") {
        continue;
      }
      std::optional<astra::Config> config;
      try {
        config = astra::Config::from_file(entry.path());
      } catch (const astra::Error&) {
        continue;  // not a config this check reads
      }
      if (!config->is_map() || !config->has("transport") ||
          config->text("transport.plugin", "") != kPeer) {
        continue;
      }
      const astra::Config earth = config->at("physical.earth");
      const std::string plugin = earth.is_map() ? earth.text("plugin") : earth.text();
      EXPECT_EQ(plugin, "plugins/physical/earth/wgs84_rotating") << entry.path();
      found.push_back(entry.path().filename().string());
    }
  }
  for (const std::string_view name : kTrackedConfigs) {
    EXPECT_NE(std::find(found.begin(), found.end(), name), found.end()) << name;
  }
  std::cout << std::format("configs naming the peer: {}\n", found.size());
}
