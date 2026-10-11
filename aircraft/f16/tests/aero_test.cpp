// The F-16 aero plugin against its source, NASA's F16_aero.dml (M7 PR 6b; R2;
// docs/plans/m7-pr6b-f16-aircraft.md, "Aero"). The oracle is the DML's own checkData, extracted by
// extract_f16_checkdata.py into f16_checkdata.hpp, which shares no code with the table generator.
//
// Pre-registered in the plan, and kept here:
// - CheckData: each shot through wrench_b, at vt = 300 ft/s, rho = 1.225 kg/m^3, identity attitude,
//   no wind, omega_ie_b = 0, controls written through the write seam in ICD v1's convention. The
//   test undoes the MRC-to-CM transfer with its own 1.132 ft, recovers the coefficients with its
//   own q-bar, S, b and c-bar, and applies the DML's criterion, |y - y_good| < tol (strict,
//   absolute; F16_aero_verify.m:686). In wrench terms that is 0.1427 N on each force, 1.305 N m on
//   roll and yaw and 0.4925 N m on pitch about the MRC.
// - InternalValues: every internal value of every shot, to 1e-12 * max(1, |v|), dimensional ones
//   converted to SI by the test. A miss there with the outputs passing goes to Joe, not into a
//   wider number.
// - SiRealizationMatchesTheDmlUnitsReference: 10,000 seeded inputs against dml_reference.hpp, to
//   1e-12 * max(1, |c|).
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../physical/aero/aero_math.hpp"
#include "astra/config/config.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/random.hpp"
#include "astra/util/units.hpp"
#include "dml_reference.hpp"
#include "f16_checkdata.hpp"

namespace {

namespace aero = astra::aircraft_f16_aero_math;
namespace checkdata = astra::aircraft_f16_checkdata;
namespace units = astra::units;

// The test's own reference geometry and transfer, from the DML's numbers, never the plugin's
// constants.
constexpr double kFoot = units::kMetersPerFoot;
constexpr double kS = 300.0 * kFoot * kFoot;
constexpr double kB = 30.0 * kFoot;
constexpr double kC = 11.32 * kFoot;
constexpr double kD = 1.132 * kFoot;  // CM forward of the MRC (D4)
constexpr double kRho = 1.225;
// NOLINTNEXTLINE(modernize-use-std-numbers): the DML's truncated pi (F16_aero.dml:355-366).
constexpr double kDmlRtd = 180.0 / 3.14159265;

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

double internal(const checkdata::Shot& shot, std::string_view var_id) {
  for (const checkdata::Internal& value : shot.internal) {
    if (value.var_id == var_id) {
      return value.value;
    }
  }
  ADD_FAILURE() << shot.name << " has no internal value " << var_id;
  return std::nan("");
}

const checkdata::Shot& shot_named(std::string_view name) {
  for (const checkdata::Shot& shot : checkdata::kAeroShots) {
    if (shot.name == name) {
      return shot;
    }
  }
  throw std::runtime_error("no shot named " + std::string{name});
}

// One flight condition in the test's terms: true airspeed (m/s), alpha and beta (rad), the
// Earth-relative rates (rad/s), and the ICD v1 controls (rad; aileron +RWD).
struct Condition {
  double vt = units::feet_to_meters(300.0);
  double alpha = 0.0;
  double beta = 0.0;
  Eigen::Vector3d omega_b = Eigen::Vector3d::Zero();
  Eigen::Vector3d omega_ie_b = Eigen::Vector3d::Zero();
  double elevator = 0.0;
  double aileron = 0.0;
  double rudder = 0.0;
};

// The state buffer; a VehicleState is a view over it (ADR-0021), so the caller keeps it alive.
astra::StateVector state_at(const Condition& c) {
  astra::StateVector y = astra::StateVector::Zero();
  y.segment<3>(astra::state::kVelocity) =
      c.vt * Eigen::Vector3d(std::cos(c.alpha) * std::cos(c.beta), std::sin(c.beta),
                             std::sin(c.alpha) * std::cos(c.beta));
  y(astra::state::kQuaternion) = 1.0;  // identity attitude; with no wind it does not matter
  y.segment<3>(astra::state::kAngularRate) = c.omega_b;
  return y;
}

// The loaded plugin with its write seam, as Vehicle would hold it.
class AeroUnderTest {
 public:
  AeroUnderTest()
      : loaded_(loader_.load(ASTRA_THIS_AIRCRAFT "/physical/aero")),
        aero_(dynamic_cast<astra::AeroModel*>(loaded_.model.get())) {
    aero_->init(astra::Config::from_string("{}", "f16-aero-test"), registry_);
    aero_->bind_inputs(inputs_, registry_);
  }

  [[nodiscard]] const astra::LoadedPlugin& loaded() const { return loaded_; }
  [[nodiscard]] const astra::InputRegistry& inputs() const { return inputs_; }

  astra::Wrench at(const Condition& c, const astra::EnvironmentSample& env) {
    inputs_.write("controls.elevator", c.elevator);
    inputs_.write("controls.aileron", c.aileron);
    inputs_.write("controls.rudder", c.rudder);
    EXPECT_TRUE(inputs_.latch().empty());
    const astra::StateVector y = state_at(c);
    return aero_->wrench_b(astra::VehicleState{y}, env);
  }

  astra::Wrench at(const Condition& c) {
    astra::EnvironmentSample env;
    env.atmosphere.density_kg_m3 = kRho;
    env.omega_ie_b = c.omega_ie_b;
    return at(c, env);
  }

 private:
  astra::PluginLoader loader_{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded_;
  astra::AeroModel* aero_;
  astra::SignalRegistry registry_;
  astra::InputRegistry inputs_;
};

// The six coefficients at the MRC, recovered from a CM wrench with the test's own constants.
struct Recovered {
  double cx = 0.0;
  double cy = 0.0;
  double cz = 0.0;
  double cl = 0.0;
  double cm = 0.0;
  double cn = 0.0;
};

Recovered recover(const astra::Wrench& w, double vt) {
  const double qs = 0.5 * kRho * vt * vt * kS;
  const double pitch_mrc = w.moment_b.y() - kD * w.force_b.z();
  const double yaw_mrc = w.moment_b.z() + kD * w.force_b.y();
  Recovered out;
  out.cx = w.force_b.x() / qs;
  out.cy = w.force_b.y() / qs;
  out.cz = w.force_b.z() / qs;
  out.cl = w.moment_b.x() / (qs * kB);
  out.cm = pitch_mrc / (qs * kC);
  out.cn = yaw_mrc / (qs * kB);
  return out;
}

// A shot's inputs as the plugin's test-side Condition (ICD convention) and as aero::Inputs (SI, the
// DML's +LWD aileron).
Condition condition_of(const checkdata::Shot& shot) {
  Condition c;
  c.vt = units::feet_to_meters(input(shot, "trueAirspeed"));
  c.alpha = units::degrees_to_radians(input(shot, "angleOfAttack"));
  c.beta = units::degrees_to_radians(input(shot, "angleOfSideslip"));
  c.omega_b =
      Eigen::Vector3d(input(shot, "bodyAngularRate_Roll"), input(shot, "bodyAngularRate_Pitch"),
                      input(shot, "bodyAngularRate_Yaw"));
  c.elevator = units::degrees_to_radians(input(shot, "elevatorDeflection"));
  c.aileron = -units::degrees_to_radians(input(shot, "aileronDeflection"));  // DML +LWD to ICD +RWD
  c.rudder = units::degrees_to_radians(input(shot, "rudderDeflection"));
  return c;
}

aero::Inputs inputs_of(const checkdata::Shot& shot) {
  aero::Inputs in;
  in.vt = units::feet_to_meters(input(shot, "trueAirspeed"));
  in.alpha = units::degrees_to_radians(input(shot, "angleOfAttack"));
  in.beta = units::degrees_to_radians(input(shot, "angleOfSideslip"));
  in.p = input(shot, "bodyAngularRate_Roll");
  in.q = input(shot, "bodyAngularRate_Pitch");
  in.r = input(shot, "bodyAngularRate_Yaw");
  in.el = units::degrees_to_radians(input(shot, "elevatorDeflection"));
  in.ail = units::degrees_to_radians(input(shot, "aileronDeflection"));
  in.rdr = units::degrees_to_radians(input(shot, "rudderDeflection"));
  return in;
}

TEST(F16Aero, DescriptorAndWriteSeamNames) {
  AeroUnderTest plugin;
  EXPECT_STREQ(plugin.loaded().descriptor->category, "physical");
  EXPECT_STREQ(plugin.loaded().descriptor->name, "f16_aero");
  EXPECT_EQ(plugin.loaded().model->name(), "aero");
  const std::vector<std::string_view> names = plugin.inputs().names();
  EXPECT_EQ(names, (std::vector<std::string_view>{"controls.elevator", "controls.aileron",
                                                  "controls.rudder"}));
}

TEST(F16Aero, CheckData) {
  ASSERT_EQ(checkdata::kAeroShots.size(), 16U);
  AeroUnderTest plugin;
  double worst = 0.0;
  for (const checkdata::Shot& shot : checkdata::kAeroShots) {
    SCOPED_TRACE(std::string{shot.name});
    const Condition c = condition_of(shot);
    const Recovered got = recover(plugin.at(c), c.vt);
    const std::vector<std::pair<std::string_view, double>> values = {
        {"referenceWingChord", aero::kCbar / kFoot},
        {"referenceWingSpan", aero::kBspan / kFoot},
        {"referenceWingArea", aero::kSref / (kFoot * kFoot)},
        {"aeroBodyForceCoefficient_X", got.cx},
        {"aeroBodyForceCoefficient_Y", got.cy},
        {"aeroBodyForceCoefficient_Z", got.cz},
        {"aeroBodyMomentCoefficient_Roll", got.cl},
        {"aeroBodyMomentCoefficient_Pitch", got.cm},
        {"aeroBodyMomentCoefficient_Yaw", got.cn},
    };
    ASSERT_EQ(shot.outputs.size(), values.size());
    for (const checkdata::Output& output : shot.outputs) {
      const auto found = std::find_if(values.begin(), values.end(),
                                      [&](const auto& v) { return v.first == output.name; });
      ASSERT_NE(found, values.end()) << output.name;
      const double error = std::abs(found->second - output.value);
      EXPECT_LT(error, output.tol)
          << output.name << ": " << found->second << " against " << output.value;
      worst = std::max(worst, error);
    }
  }
  std::cout << "F16Aero.CheckData: largest |y - y_good| over 16 shots x 9 outputs = " << worst
            << " (tol 1e-6)\n";
}

// How each internalValues varID maps to a BuildUp field, and what converts the DML's value to SI.
enum class Kind : std::uint8_t { kDimensionless, kAngle, kSpeed, kLength, kArea };

struct Field {
  std::string_view var_id;
  double aero::BuildUp::*member;
  Kind kind;
};

const std::vector<Field>& fields() {
  using B = aero::BuildUp;
  static const std::vector<Field> kAll = {
      {"vt", &B::vt, Kind::kSpeed},
      {"alpha", &B::alpha, Kind::kAngle},
      {"beta", &B::beta, Kind::kAngle},
      {"p", &B::p, Kind::kDimensionless},
      {"q", &B::q, Kind::kDimensionless},
      {"r", &B::r, Kind::kDimensionless},
      {"el", &B::el, Kind::kAngle},
      {"ail", &B::ail, Kind::kAngle},
      {"rdr", &B::rdr, Kind::kAngle},
      {"cbar", &B::cbar, Kind::kLength},
      {"bspan", &B::bspan, Kind::kLength},
      {"sref", &B::sref, Kind::kArea},
      {"del", &B::del, Kind::kDimensionless},
      {"dail", &B::dail, Kind::kDimensionless},
      {"drdr", &B::drdr, Kind::kDimensionless},
      {"cxt", &B::cxt, Kind::kDimensionless},
      {"cy0", &B::cy0, Kind::kDimensionless},
      {"czt", &B::czt, Kind::kDimensionless},
      {"cz1", &B::cz1, Kind::kDimensionless},
      {"tvt", &B::tvt, Kind::kSpeed},
      {"b2v", &B::b2v, Kind::kDimensionless},
      {"cq2v", &B::cq2v, Kind::kDimensionless},
      {"absbeta", &B::absbeta, Kind::kAngle},
      {"absCl0", &B::abs_cl0, Kind::kDimensionless},
      {"absCn0", &B::abs_cn0, Kind::kDimensionless},
      {"clt", &B::clt, Kind::kDimensionless},
      {"cmt", &B::cmt, Kind::kDimensionless},
      {"cnt", &B::cnt, Kind::kDimensionless},
      {"cxq", &B::cxq, Kind::kDimensionless},
      {"cyr", &B::cyr, Kind::kDimensionless},
      {"cyp", &B::cyp, Kind::kDimensionless},
      {"czq", &B::czq, Kind::kDimensionless},
      {"clr", &B::clr, Kind::kDimensionless},
      {"clp", &B::clp, Kind::kDimensionless},
      {"cmq", &B::cmq, Kind::kDimensionless},
      {"cnr", &B::cnr, Kind::kDimensionless},
      {"cnp", &B::cnp, Kind::kDimensionless},
      {"dclda", &B::dclda, Kind::kDimensionless},
      {"dcldr", &B::dcldr, Kind::kDimensionless},
      {"dcnda", &B::dcnda, Kind::kDimensionless},
      {"dcndr", &B::dcndr, Kind::kDimensionless},
      {"cl1", &B::cl1, Kind::kDimensionless},
      {"cn1", &B::cn1, Kind::kDimensionless},
      {"cx", &B::cx, Kind::kDimensionless},
      {"cy", &B::cy, Kind::kDimensionless},
      {"cz", &B::cz, Kind::kDimensionless},
      {"cl", &B::cl, Kind::kDimensionless},
      {"cm", &B::cm, Kind::kDimensionless},
      {"cn", &B::cn, Kind::kDimensionless},
  };
  return kAll;
}

double to_si(double dml, Kind kind) {
  switch (kind) {
    case Kind::kAngle:
      return units::degrees_to_radians(dml);
    case Kind::kSpeed:
    case Kind::kLength:
      return units::feet_to_meters(dml);
    case Kind::kArea:
      return units::feet_to_meters(units::feet_to_meters(dml));
    case Kind::kDimensionless:
      break;
  }
  return dml;
}

// b2v (s) and cq2v (dimensionless) are the same numbers in either unit system, and p, q, r are
// rad/s in both. rtd is a constant, pinned in RtdIsTheDmlsTruncatedConstant.
TEST(F16Aero, InternalValues) {
  double worst = 0.0;
  for (const checkdata::Shot& shot : checkdata::kAeroShots) {
    SCOPED_TRACE(std::string{shot.name});
    ASSERT_EQ(shot.internal.size(), 50U);
    const aero::BuildUp b = aero::build_up(inputs_of(shot));
    std::size_t matched = 0;
    for (const checkdata::Internal& value : shot.internal) {
      if (value.var_id == "rtd") {
        continue;
      }
      const auto field = std::find_if(fields().begin(), fields().end(),
                                      [&](const Field& f) { return f.var_id == value.var_id; });
      ASSERT_NE(field, fields().end()) << "no BuildUp field for " << value.var_id;
      const double expected = to_si(value.value, field->kind);
      const double got = b.*(field->member);
      const double scale = std::max(1.0, std::abs(expected));
      EXPECT_LE(std::abs(got - expected), 1e-12 * scale)
          << value.var_id << ": " << got << " against " << expected;
      worst = std::max(worst, std::abs(got - expected) / scale);
      ++matched;
    }
    EXPECT_EQ(matched, fields().size());
  }
  std::cout << "F16Aero.InternalValues: largest |got - expected| / max(1, |expected|) = " << worst
            << " (tol 1e-12)\n";
}

TEST(F16Aero, RtdIsTheDmlsTruncatedConstant) {
  EXPECT_EQ(bits(aero::kRtd), bits(180.0 / 3.14159265));
  const double shot_rtd = internal(checkdata::kAeroShots[0], "rtd");
  EXPECT_LE(std::abs(aero::kRtd - shot_rtd), 1e-12);
  // The negative control: the SI 180/pi misses the shots' value by about 6.5e-8.
  EXPECT_GT(std::abs(180.0 / std::numbers::pi - shot_rtd), 1e-8);
}

TEST(F16Aero, SiRealizationMatchesTheDmlUnitsReference) {
  astra::random::Rng rng(20261010U);
  double worst = 0.0;
  for (int i = 0; i < 10000; ++i) {
    // Degrees and ft/s as drawn; the SI side converts them through core/util/units.
    const double alpha = rng.uniform(-30.0, 70.0);
    const double beta = rng.uniform(-45.0, 45.0);
    const double el = rng.uniform(-40.0, 40.0);
    const double ail = rng.uniform(-40.0, 40.0);
    const double rdr = rng.uniform(-40.0, 40.0);
    const double p = rng.uniform(-2.0, 2.0);
    const double q = rng.uniform(-2.0, 2.0);
    const double r = rng.uniform(-2.0, 2.0);
    // A fifth of the draws below 1 ft/s, so the 0.1 ft/s floor is reached often.
    const double vt_ft = i % 5 == 0 ? rng.uniform(0.0, 1.0) : rng.uniform(0.0, 984.0);

    const f16_dml_reference::Coefficients want =
        f16_dml_reference::evaluate({vt_ft, alpha, beta, p, q, r, el, ail, rdr});
    aero::Inputs in;
    in.vt = units::feet_to_meters(vt_ft);
    in.alpha = units::degrees_to_radians(alpha);
    in.beta = units::degrees_to_radians(beta);
    in.p = p;
    in.q = q;
    in.r = r;
    in.el = units::degrees_to_radians(el);
    in.ail = units::degrees_to_radians(ail);
    in.rdr = units::degrees_to_radians(rdr);
    const aero::BuildUp got = aero::build_up(in);

    const std::array<std::pair<double, double>, 6> pairs = {{{got.cx, want.cx},
                                                             {got.cy, want.cy},
                                                             {got.cz, want.cz},
                                                             {got.cl, want.cl},
                                                             {got.cm, want.cm},
                                                             {got.cn, want.cn}}};
    for (const auto& [si, dml] : pairs) {
      const double scale = std::max(1.0, std::abs(dml));
      ASSERT_LE(std::abs(si - dml), 1e-12 * scale)
          << "draw " << i << ": alpha " << alpha << ", beta " << beta << ", vt " << vt_ft;
      worst = std::max(worst, std::abs(si - dml) / scale);
    }
  }
  std::cout << "F16Aero.SiRealizationMatchesTheDmlUnitsReference: largest relative difference = "
            << worst << " (tol 1e-12)\n";
}

// R2, critique C-05: in ICD v1's convention, a positive aileron rolls right wing down, a positive
// elevator (+TED) pitches the nose down, and a positive rudder (+TEL) yaws the nose left
// (ADR-0001).
TEST(F16Aero, ControlSigns) {
  AeroUnderTest plugin;
  Condition base;
  base.alpha = units::degrees_to_radians(5.0);
  const astra::Wrench neutral = plugin.at(base);

  Condition aileron = base;
  aileron.aileron = 0.1;
  EXPECT_GT(plugin.at(aileron).moment_b.x(), 0.0);

  Condition elevator = base;
  elevator.elevator = 0.1;
  EXPECT_LT(plugin.at(elevator).moment_b.y(), neutral.moment_b.y());

  Condition rudder = base;
  rudder.rudder = 0.1;
  EXPECT_LT(plugin.at(rudder).moment_b.z(), neutral.moment_b.z());

  // The "Positive aileron" shot (F16_aero.dml:3291): DML ail = +24.1 deg (+LWD) is ICD -24.1 deg,
  // and gives cl = -0.06266, a left-wing-down roll; ICD +24.1 deg rolls right wing down.
  Condition anchor = base;
  anchor.aileron = -units::degrees_to_radians(24.1);
  const astra::Wrench left = plugin.at(anchor);
  EXPECT_LT(std::abs(recover(left, anchor.vt).cl - -0.06266), 1e-6);
  EXPECT_LT(left.moment_b.x(), 0.0);
  anchor.aileron = units::degrees_to_radians(24.1);
  EXPECT_GT(plugin.at(anchor).moment_b.x(), 0.0);
}

// Q15: each table holds its edge value past each end, bit for bit, and its corners are the source's
// own entries, transcribed here from F16_aero.dml by line (independently of the generator).
struct Table {
  std::string_view name;
  std::function<double(const aero::BuildUp&)> value;
  bool two_d;
  bool row_is_beta1;  // rows on |beta| (BETA1)
  bool row_is_beta2;  // rows on signed beta (BETA2); otherwise rows on el (DE1) when two_d
  std::array<double, 4> corner;  // 2-D: [row lo][alpha lo], [row lo][alpha hi], [row hi][alpha lo],
                                 // [row hi][alpha hi]; 1-D: [alpha lo], [alpha hi]
};

aero::BuildUp at_degrees(double alpha_deg, double row_deg, bool row_is_beta, bool row_is_el) {
  aero::Inputs in;
  in.vt = units::feet_to_meters(300.0);
  in.alpha = units::degrees_to_radians(alpha_deg);
  if (row_is_beta) {
    in.beta = units::degrees_to_radians(row_deg);
  }
  if (row_is_el) {
    in.el = units::degrees_to_radians(row_deg);
  }
  return aero::build_up(in);
}

TEST(F16Aero, TableEdgesClip) {
  const std::vector<Table> all = {
      // Line numbers are F16_aero.dml's.
      {"cxt",
       [](const aero::BuildUp& b) { return b.cxt; },
       true,
       false,
       false,
       {-.099, .166, -.083, .040}},  // :1001, :1001, :1005, :1005
      {"czt",
       [](const aero::BuildUp& b) { return b.czt; },
       false,
       false,
       false,
       {.770, -2.229, 0, 0}},  // :1030, :1031
      {"cmt",
       [](const aero::BuildUp& b) { return b.cmt; },
       true,
       false,
       false,
       {.205, .192, -.259, -.005}},  // :1067, :1067, :1071, :1071
      {"absCl0",
       [](const aero::BuildUp& b) { return b.abs_cl0; },
       true,
       true,
       false,
       {0., 0., .009, -.076}},  // :1101, :1102, :1107, :1108
      {"absCn0",
       [](const aero::BuildUp& b) { return b.abs_cn0; },
       true,
       true,
       false,
       {0., 0., .079, -.001}},  // :1139, :1139, :1146, :1147
      {"cxq",
       [](const aero::BuildUp& b) { return b.cxq; },
       false,
       false,
       false,
       {-.267, 1.21, 0, 0}},  // :1179
      {"cyr",
       [](const aero::BuildUp& b) { return b.cyr; },
       false,
       false,
       false,
       {.882, -1.04, 0, 0}},  // :1205
      {"cyp",
       [](const aero::BuildUp& b) { return b.cyp; },
       false,
       false,
       false,
       {-.108, -.227, 0, 0}},  // :1231, :1232
      {"czq",
       [](const aero::BuildUp& b) { return b.czq; },
       false,
       false,
       false,
       {-8.80, -35.3, 0, 0}},  // :1257, :1258
      {"clr",
       [](const aero::BuildUp& b) { return b.clr; },
       false,
       false,
       false,
       {-.126, -.330, 0, 0}},  // :1283, :1284
      {"clp",
       [](const aero::BuildUp& b) { return b.clp; },
       false,
       false,
       false,
       {-.360, -.100, 0, 0}},  // :1309, :1310
      {"cmq",
       [](const aero::BuildUp& b) { return b.cmq; },
       false,
       false,
       false,
       {-7.21, -6.00, 0, 0}},  // :1338, :1339
      {"cnr",
       [](const aero::BuildUp& b) { return b.cnr; },
       false,
       false,
       false,
       {-.380, -.840, 0, 0}},  // :1364, :1365
      {"cnp",
       [](const aero::BuildUp& b) { return b.cnp; },
       false,
       false,
       false,
       {.061, .150, 0, 0}},  // :1390, :1391
      {"dclda",
       [](const aero::BuildUp& b) { return b.dclda; },
       true,
       false,
       true,
       {-.041, -.017, -.043, -.008}},  // :1426, :1427, :1434, :1435
      {"dcldr",
       [](const aero::BuildUp& b) { return b.dcldr; },
       true,
       false,
       true,
       {.005, .008, .023, .000}},  // :1467, :1467, :1474, :1475
      {"dcnda",
       [](const aero::BuildUp& b) { return b.dcnda; },
       true,
       false,
       true,
       {.001, .016, -.022, .001}},  // :1507, :1507, :1514, :1515
      {"dcndr",
       [](const aero::BuildUp& b) { return b.dcndr; },
       true,
       false,
       true,
       {-.018, -.013, -.062, -.010}},  // :1548, :1549, :1556, :1557
  };
  ASSERT_EQ(all.size(), 18U);

  for (const Table& t : all) {
    SCOPED_TRACE(std::string{t.name});
    const bool on_beta = t.row_is_beta1 || t.row_is_beta2;
    const bool on_el = t.two_d && !on_beta;
    // The row axis's ends and the values past them; BETA1 is read at |beta|, so -40 deg is past its
    // upper end too.
    double row_lo = -24.0;  // DE1
    double row_hi = 24.0;
    if (t.row_is_beta1) {
      row_lo = 0.0;
      row_hi = 30.0;
    } else if (t.row_is_beta2) {
      row_lo = -30.0;
      row_hi = 30.0;
    }
    const double row_past_lo = t.row_is_beta1 ? -0.0 : row_lo - 10.0;
    const double row_past_hi = row_hi + 10.0;

    auto value = [&](double alpha_deg, double row_deg) {
      return t.value(at_degrees(alpha_deg, row_deg, on_beta, on_el));
    };
    // Past each alpha end, at every row breakpoint, holds the edge value.
    for (const double row : {row_lo, 0.5 * (row_lo + row_hi), row_hi}) {
      EXPECT_EQ(bits(value(-20.0, row)), bits(value(-10.0, row))) << "row " << row;
      EXPECT_EQ(bits(value(60.0, row)), bits(value(45.0, row))) << "row " << row;
    }
    if (t.two_d) {
      // Past each row end, at alpha ends and inside, holds the edge value.
      for (const double alpha : {-10.0, 7.5, 45.0}) {
        EXPECT_EQ(bits(value(alpha, row_past_lo)), bits(value(alpha, row_lo))) << alpha;
        EXPECT_EQ(bits(value(alpha, row_past_hi)), bits(value(alpha, row_hi))) << alpha;
      }
      if (t.row_is_beta1) {
        EXPECT_EQ(bits(value(7.5, -40.0)), bits(value(7.5, 30.0)));
      }
      EXPECT_EQ(value(-10.0, row_lo), t.corner[0]);
      EXPECT_EQ(value(45.0, row_lo), t.corner[1]);
      EXPECT_EQ(value(-10.0, row_hi), t.corner[2]);
      EXPECT_EQ(value(45.0, row_hi), t.corner[3]);
      // The corners past both ends at once.
      EXPECT_EQ(value(-20.0, row_past_hi), t.corner[2]);
      EXPECT_EQ(value(60.0, row_past_hi), t.corner[3]);
    } else {
      EXPECT_EQ(value(-10.0, 0.0), t.corner[0]);
      EXPECT_EQ(value(45.0, 0.0), t.corner[1]);
    }
  }
}

// Critique F-2: only the lookups clip; del, beta (in cy0 and cz1) and drdr keep acting past the
// tables' ranges. Expected differences are written here from the DML's equations, with the
// derivatives taken from the "Positive aileron" shot's internal values (alpha 5 deg, beta 0).
TEST(F16Aero, OnlyTheLookupsClip) {
  AeroUnderTest plugin;
  const checkdata::Shot& shot = shot_named("Positive aileron");
  Condition base;
  base.alpha = units::degrees_to_radians(5.0);
  auto coefficients = [&](const Condition& c) { return recover(plugin.at(c), c.vt); };

  Condition el24 = base;
  el24.elevator = units::degrees_to_radians(24.0);
  Condition el30 = base;
  el30.elevator = units::degrees_to_radians(30.0);
  const Recovered a = coefficients(el24);
  const Recovered b = coefficients(el30);
  EXPECT_NEAR(b.cx, a.cx, 1e-12);  // DE1 holds at 24 deg
  EXPECT_NEAR(b.cm, a.cm, 1e-12);
  EXPECT_NEAR(b.cz - a.cz, -0.19 * (30.0 - 24.0) / 25.0, 1e-12);  // -0.0456: del is unclipped

  Condition beta30 = base;
  beta30.beta = units::degrees_to_radians(30.0);
  Condition beta40 = base;
  beta40.beta = units::degrees_to_radians(40.0);
  const Recovered c30 = coefficients(beta30);
  const Recovered c40 = coefficients(beta40);
  EXPECT_NEAR(c40.cl, c30.cl, 1e-12);  // BETA1 (on |beta|) and BETA2 hold at 30 deg
  EXPECT_NEAR(c40.cn, c30.cn, 1e-12);
  EXPECT_NEAR(c40.cy - c30.cy, -0.02 * (40.0 - 30.0), 1e-12);  // -0.2: cy0's beta is unclipped
  const double czt = internal(shot, "czt");                    // -0.416 at alpha 5 deg
  const double expected_dcz = czt * (std::pow(30.0 / kDmlRtd, 2) - std::pow(40.0 / kDmlRtd, 2));
  EXPECT_NEAR(c40.cz - c30.cz, expected_dcz, 1e-12);  // about +0.0887: cz1's beta is unclipped

  Condition rudder45 = base;
  rudder45.rudder = units::degrees_to_radians(45.0);  // drdr = 1.5, past the normalization's 30 deg
  const Recovered r0 = coefficients(base);
  const Recovered r45 = coefficients(rudder45);
  EXPECT_NEAR(r45.cy - r0.cy, 0.086 * 1.5, 1e-12);
  EXPECT_NEAR(r45.cl - r0.cl, internal(shot, "dcldr") * 1.5, 1e-12);
  EXPECT_NEAR(r45.cn - r0.cn, internal(shot, "dcndr") * 1.5, 1e-12);
  // A clip at 30 deg would miss these by 0.043, 0.007 and 0.0225.
}

// Critique F-3 and Joe's amendment 4: the floor is 0.1 ft/s and feeds only b2v; q-bar uses the true
// airspeed. The test takes the difference between p = 1 and p = 0 rad/s, so it rests on no premise
// about cl1 (which is 0 here only because Cl0's beta = 0 row is all zeros, F16_aero.dml:1101).
TEST(F16Aero, TheSpeedFloorIsPointOneFeetPerSecond) {
  AeroUnderTest plugin;
  const double clp = internal(shot_named("Positive aileron"), "clp");  // -0.42 at alpha 5 deg
  for (const double vt_ft : {0.05, 0.2}) {
    SCOPED_TRACE(vt_ft);
    Condition still;
    still.vt = vt_ft * kFoot;
    still.alpha = units::degrees_to_radians(5.0);
    Condition rolling = still;
    rolling.omega_b = Eigen::Vector3d(1.0, 0.0, 0.0);
    const double difference = plugin.at(rolling).moment_b.x() - plugin.at(still).moment_b.x();

    const double qbar = 0.5 * kRho * still.vt * still.vt;  // the true airspeed, never floored
    const double floored = std::max(vt_ft, 0.1) * kFoot;
    const double expected = qbar * kS * kB * clp * kB / (2.0 * floored);
    EXPECT_NEAR(difference, expected, 1e-12 * std::abs(expected));
  }
}

// ADR-0032: with omega_ie_b equal to the state's rate (a vehicle turning with the Earth), the
// wrench is the zero-rate wrench bit for bit; the rates do move it on a flat Earth.
TEST(F16Aero, DampsTheEarthRelativeRate) {
  AeroUnderTest plugin;
  Condition turning;
  turning.alpha = units::degrees_to_radians(5.0);
  turning.beta = units::degrees_to_radians(2.0);
  turning.omega_b = Eigen::Vector3d(0.05, -0.02, 0.03);
  turning.omega_ie_b = turning.omega_b;
  Condition still = turning;
  still.omega_b = Eigen::Vector3d::Zero();
  still.omega_ie_b = Eigen::Vector3d::Zero();
  Condition rotating_on_flat = turning;
  rotating_on_flat.omega_ie_b = Eigen::Vector3d::Zero();

  const astra::Wrench relative = plugin.at(turning);
  const astra::Wrench zero_rate = plugin.at(still);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(bits(relative.force_b(i)), bits(zero_rate.force_b(i)));
    EXPECT_EQ(bits(relative.moment_b(i)), bits(zero_rate.moment_b(i)));
  }
  EXPECT_NE(plugin.at(rotating_on_flat).moment_b, zero_rate.moment_b);
}

TEST(F16Aero, AnUnsetEarthRateGivesANonFiniteWrench) {
  AeroUnderTest plugin;
  Condition c;
  c.alpha = units::degrees_to_radians(5.0);
  astra::EnvironmentSample env;
  env.atmosphere.density_kg_m3 = kRho;  // omega_ie_b left at its NaN default
  const astra::Wrench unset = plugin.at(c, env);
  EXPECT_FALSE(unset.force_b.allFinite());
  EXPECT_FALSE(unset.moment_b.allFinite());
  env.omega_ie_b = Eigen::Vector3d::Zero();
  EXPECT_TRUE(plugin.at(c, env).force_b.allFinite());
}

TEST(F16Aero, ZeroAirspeedIsFiniteAndZero) {
  AeroUnderTest plugin;
  Condition c;
  c.vt = 0.0;
  c.omega_b = Eigen::Vector3d(0.3, -0.2, 0.1);
  c.elevator = 0.1;
  const astra::Wrench w = plugin.at(c);
  EXPECT_TRUE(w.force_b.allFinite());
  EXPECT_TRUE(w.moment_b.allFinite());
  EXPECT_EQ(w.force_b, Eigen::Vector3d::Zero());
  EXPECT_EQ(w.moment_b, Eigen::Vector3d::Zero());
}

// D6: the Mach limit PR 13 will declare (ADR-0031); nothing here reads it.
TEST(F16Aero, MachLimitIsOneForPr13) { EXPECT_EQ(aero::kMachLimit, 1.0); }

}  // namespace
