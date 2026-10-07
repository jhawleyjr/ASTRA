// The F-16 NESC band headers (ADR-0028, M7 PR 8; docs/plans/m7-pr8-nesc-bands.md), checked before
// any harness flies them (PR 9, 11a, 11b). Every expected value below is restated from its source,
// never read from the generator: the sample counts and the first gated sample from ADR-0028 items 3
// and 5, the floors from ADR-0014's table and ADR-0015's atmosphere conversion, and the initial
// conditions from Vol II Tables 35 to 40, 42 and 43 in their published units, converted here with
// core/util/units.
//
// The initial-condition check (Joe's amendment 2 to the PR 8 plan) compares each case's sample-0
// reference of latitude, longitude, altitude, v_north, v_east, v_down, roll and yaw with Vol II's
// printed initial condition. Pitch is trimmed, so it is not checked. Its purpose is to catch a
// wrong file, column, unit or sign in the generated header: a wrong file (another case's data,
// except as below), a swapped column, a missing degree or foot conversion, or a flipped sign each
// moves some value far outside its tolerance. It cannot tell case 11 from 13.1 to 13.4, which share
// their initial conditions (Tables 35, 37 to 40), and it checks only t = 0, so it says nothing
// about the band's width or any later sample.
//
// The tolerance is max(half a unit in the last digit Vol II prints, 1e-6 x |printed value|), in the
// published unit, converted with the same function (Joe's decision, 2026-10-07, for every value in
// every case). A printed integer, including 0, has a half unit of 0.5. The first rule, half a digit
// alone, tested the precision of NASA's printed table rather than this purpose: case 12's v_north
// and v_east missed it (the members' mean is 1414.2135610 ft/s against the printed 1414.213562). A
// miss is a finding for Joe; no tolerance here is loosened to clear one.
//
// Each sweep counts what it visited and asserts 8 cases and 21 channels per case, so a sweep that
// visits nothing cannot pass (docs/lessons.md, 2026-09-29).
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "astra/util/units.hpp"
#include "golden/nesc/case11_trim_check_subsonic_f16_band.hpp"
#include "golden/nesc/case12_trim_check_supersonic_f16_band.hpp"
#include "golden/nesc/case13p1_subsonic_altitude_change_f16_band.hpp"
#include "golden/nesc/case13p2_subsonic_airspeed_change_f16_band.hpp"
#include "golden/nesc/case13p3_subsonic_heading_change_f16_band.hpp"
#include "golden/nesc/case13p4_subsonic_lateral_side_step_f16_band.hpp"
#include "golden/nesc/case15_circle_north_pole_band.hpp"
#include "golden/nesc/case16_circle_equator_date_line_band.hpp"
#include "golden/nesc/nesc_band_common.hpp"

namespace {

namespace nesc = astra::test::nesc;
using astra::units::degrees_to_radians;
using astra::units::feet_to_meters;

constexpr int kCaseCount = 8;
constexpr int kChannelCount = 21;
constexpr int kInitialConditionChannelCount = 8;

// ADR-0013's cases 1 to 10: 301 samples, every sample gated.
static_assert(nesc::CaseBand::kSampleCount == 301);
static_assert(nesc::CaseBand::kFirstGatedSample == 0);

// ADR-0028 item 3 (Vol II's durations: 180 s for 11, 12, 15 and 16; 20 s for 13.1 and 13.2; 30 s
// for 13.3; 60 s for 13.4) and item 5 (t >= 8 s, so the first gated sample is 80).
constexpr int kFirstGatedSample = 80;
static_assert(nesc::case11::Band::kSampleCount == 1801);
static_assert(nesc::case12::Band::kSampleCount == 1801);
static_assert(nesc::case13p1::Band::kSampleCount == 201);
static_assert(nesc::case13p2::Band::kSampleCount == 201);
static_assert(nesc::case13p3::Band::kSampleCount == 301);
static_assert(nesc::case13p4::Band::kSampleCount == 601);
static_assert(nesc::case15::Band::kSampleCount == 1801);
static_assert(nesc::case16::Band::kSampleCount == 1801);
static_assert(nesc::case11::Band::kFirstGatedSample == kFirstGatedSample);
static_assert(nesc::case12::Band::kFirstGatedSample == kFirstGatedSample);
static_assert(nesc::case13p1::Band::kFirstGatedSample == kFirstGatedSample);
static_assert(nesc::case13p2::Band::kFirstGatedSample == kFirstGatedSample);
static_assert(nesc::case13p3::Band::kFirstGatedSample == kFirstGatedSample);
static_assert(nesc::case13p4::Band::kFirstGatedSample == kFirstGatedSample);
static_assert(nesc::case15::Band::kFirstGatedSample == kFirstGatedSample);
static_assert(nesc::case16::Band::kFirstGatedSample == kFirstGatedSample);

enum class Kind { kAngle, kLength, kVelocity, kRate, kForce, kDensity, kPressure, kTemperature };

// ADR-0014's per-kind floors; ADR-0015 converts the atmosphere kinds' 1e-12 relative floor into an
// absolute one at ISA-76's sea-level values (1.225 kg/m^3, 101325 Pa, 288.15 K).
double floor_of(Kind kind) {
  switch (kind) {
    case Kind::kAngle:
      return 1e-12;  // rad
    case Kind::kLength:
      return 1e-9;  // m
    case Kind::kVelocity:
      return 1e-9;  // m/s
    case Kind::kRate:
      return 1e-12;  // rad/s
    case Kind::kForce:
      return 1e-12;  // N or N*m
    case Kind::kDensity:
      return 1e-12 * 1.225;  // kg/m^3
    case Kind::kPressure:
      return 1e-12 * 101'325.0;  // Pa
    case Kind::kTemperature:
      return 1e-12 * 288.15;  // K
  }
  return 0.0;
}

// The shortest signed angular distance, in (-pi, pi]: ADR-0028 item 6 compares yaw and longitude on
// the wrapped difference.
double wrap_to_pi(double x) {
  double wrapped = std::fmod(x + std::numbers::pi, 2.0 * std::numbers::pi);
  if (wrapped <= 0.0) {
    wrapped += 2.0 * std::numbers::pi;
  }
  return wrapped - std::numbers::pi;
}

struct ChannelView {
  std::string_view name;
  Kind kind;
  bool wrapped;
  std::span<const double> reference;
  double tol_abs;
};

struct CaseView {
  std::string_view name;
  int sample_count;
  int first_gated_sample;
  std::array<ChannelView, kChannelCount> channels;
};

template <typename Band>
CaseView view_of(std::string_view name, const Band& band) {
  using B = std::remove_cvref_t<Band>;
  return CaseView{
      name,
      B::kSampleCount,
      B::kFirstGatedSample,
      {{
          {"latitude", Kind::kAngle, false, band.latitude.reference, band.latitude.tol_abs},
          {"longitude", Kind::kAngle, true, band.longitude.reference, band.longitude.tol_abs},
          {"altitude", Kind::kLength, false, band.altitude.reference, band.altitude.tol_abs},
          {"v_north", Kind::kVelocity, false, band.v_north.reference, band.v_north.tol_abs},
          {"v_east", Kind::kVelocity, false, band.v_east.reference, band.v_east.tol_abs},
          {"v_down", Kind::kVelocity, false, band.v_down.reference, band.v_down.tol_abs},
          {"yaw", Kind::kAngle, true, band.yaw.reference, band.yaw.tol_abs},
          {"pitch", Kind::kAngle, false, band.pitch.reference, band.pitch.tol_abs},
          {"roll", Kind::kAngle, false, band.roll.reference, band.roll.tol_abs},
          {"rate_p", Kind::kRate, false, band.rate_p.reference, band.rate_p.tol_abs},
          {"rate_q", Kind::kRate, false, band.rate_q.reference, band.rate_q.tol_abs},
          {"rate_r", Kind::kRate, false, band.rate_r.reference, band.rate_r.tol_abs},
          {"force_x", Kind::kForce, false, band.force_x.reference, band.force_x.tol_abs},
          {"force_y", Kind::kForce, false, band.force_y.reference, band.force_y.tol_abs},
          {"force_z", Kind::kForce, false, band.force_z.reference, band.force_z.tol_abs},
          {"moment_l", Kind::kForce, false, band.moment_l.reference, band.moment_l.tol_abs},
          {"moment_m", Kind::kForce, false, band.moment_m.reference, band.moment_m.tol_abs},
          {"moment_n", Kind::kForce, false, band.moment_n.reference, band.moment_n.tol_abs},
          {"density", Kind::kDensity, false, band.density.reference, band.density.tol_abs},
          {"pressure", Kind::kPressure, false, band.pressure.reference, band.pressure.tol_abs},
          {"temperature", Kind::kTemperature, false, band.temperature.reference,
           band.temperature.tol_abs},
      }},
  };
}

std::vector<CaseView> f16_cases() {
  return {
      view_of("case11", nesc::case11::kBand),     view_of("case12", nesc::case12::kBand),
      view_of("case13p1", nesc::case13p1::kBand), view_of("case13p2", nesc::case13p2::kBand),
      view_of("case13p3", nesc::case13p3::kBand), view_of("case13p4", nesc::case13p4::kBand),
      view_of("case15", nesc::case15::kBand),     view_of("case16", nesc::case16::kBand),
  };
}

struct ExpectedShape {
  std::string_view name;
  int sample_count;
};

// ADR-0028 item 3, restated (see the static_asserts above for the durations).
constexpr std::array<ExpectedShape, kCaseCount> kExpectedShapes{{
    {"case11", 1801},
    {"case12", 1801},
    {"case13p1", 201},
    {"case13p2", 201},
    {"case13p3", 301},
    {"case13p4", 601},
    {"case15", 1801},
    {"case16", 1801},
}};

TEST(NescF16Band, SampleCountsAndWindow) {
  const std::vector<CaseView> cases = f16_cases();
  ASSERT_EQ(cases.size(), kExpectedShapes.size());
  int visited_cases = 0;
  for (std::size_t i = 0; i < cases.size(); ++i) {
    const CaseView& c = cases[i];
    EXPECT_EQ(c.name, kExpectedShapes.at(i).name);
    EXPECT_EQ(c.sample_count, kExpectedShapes.at(i).sample_count) << c.name;
    EXPECT_EQ(c.first_gated_sample, kFirstGatedSample) << c.name;
    int visited_channels = 0;
    for (const ChannelView& ch : c.channels) {
      EXPECT_EQ(ch.reference.size(), static_cast<std::size_t>(c.sample_count))
          << c.name << " " << ch.name;
      ++visited_channels;
    }
    EXPECT_EQ(visited_channels, kChannelCount) << c.name;
    ++visited_cases;
  }
  EXPECT_EQ(visited_cases, kCaseCount);
}

TEST(NescF16Band, FloorsHold) {
  int visited_cases = 0;
  for (const CaseView& c : f16_cases()) {
    int visited_channels = 0;
    for (const ChannelView& ch : c.channels) {
      EXPECT_TRUE(std::isfinite(ch.tol_abs)) << c.name << " " << ch.name;
      EXPECT_GE(ch.tol_abs, floor_of(ch.kind)) << c.name << " " << ch.name;
      ++visited_channels;
    }
    EXPECT_EQ(visited_channels, kChannelCount) << c.name;
    ++visited_cases;
  }
  EXPECT_EQ(visited_cases, kCaseCount);
}

TEST(NescF16Band, ReferencesFinite) {
  int visited_cases = 0;
  for (const CaseView& c : f16_cases()) {
    int visited_channels = 0;
    for (const ChannelView& ch : c.channels) {
      int non_finite = 0;
      int outside_pi = 0;
      for (const double v : ch.reference) {
        if (!std::isfinite(v)) {
          ++non_finite;
        }
        if (ch.wrapped && std::abs(v) > std::numbers::pi) {
          ++outside_pi;
        }
      }
      EXPECT_EQ(non_finite, 0) << c.name << " " << ch.name;
      EXPECT_EQ(outside_pi, 0) << c.name << " " << ch.name
                               << ": wrapped reference outside [-pi, pi]";
      if (ch.kind == Kind::kAngle) {
        EXPECT_LE(ch.tol_abs, std::numbers::pi) << c.name << " " << ch.name;
      }
      ++visited_channels;
    }
    EXPECT_EQ(visited_channels, kChannelCount) << c.name;
    ++visited_cases;
  }
  EXPECT_EQ(visited_cases, kCaseCount);
}

// One value as Vol II prints it, with half a unit in its last printed digit, both in the published
// unit.
struct Printed {
  double value;
  double half_digit;
};

// Joe's rule (2026-10-07): max(half a unit in the last printed digit, 1e-6 x |printed value|).
constexpr double kRelativeAllowance = 1e-6;
double tolerance_of(Printed p) {
  return std::max(p.half_digit, kRelativeAllowance * std::abs(p.value));
}

struct VolIIInitialCondition {
  std::string_view name;
  Printed latitude_deg;   // Geodetic position (deg, deg, ft MSL)
  Printed longitude_deg;  //
  Printed altitude_ft;    //
  Printed v_north_ft_s;   // Local-relative velocity [north, east, down], ft/s
  Printed v_east_ft_s;    //
  Printed v_down_ft_s;    //
  Printed roll_deg;       // Attitude [roll, pitch, yaw], deg; pitch is trimmed and not checked
  Printed yaw_deg;        //
};

// Tables 35 to 40 print the same initial state for cases 11, 13.1, 13.2, 13.3 and 13.4.
constexpr VolIIInitialCondition subsonic_case(std::string_view name) {
  return {name,
          {36.01916667, 5e-9},   // [36.01916667, -75.67444444, 10013]
          {-75.67444444, 5e-9},  //
          {10013.0, 0.5},        //
          {400.0, 0.5},          // [400, 400, 0]
          {400.0, 0.5},          //
          {0.0, 0.5},            //
          {0.0, 0.5},            // [0, 0, 45]
          {45.0, 0.5}};
}

const std::array<VolIIInitialCondition, kCaseCount> kInitialConditions{{
    subsonic_case("case11"),  // Table 35
    {"case12",                // Table 36
     {36.01916667, 5e-9},     // [36.01916667, -75.67444444, 30013]
     {-75.67444444, 5e-9},
     {30013.0, 0.5},
     {1414.213562, 5e-7},  // [1414.213562, 1414.213562, 0]
     {1414.213562, 5e-7},
     {0.0, 0.5},
     {0.0, 0.5},  // [0, 0, 45]
     {45.0, 0.5}},
    subsonic_case("case13p1"),  // Table 37
    subsonic_case("case13p2"),  // Table 38
    subsonic_case("case13p3"),  // Table 39
    subsonic_case("case13p4"),  // Table 40
    {"case15",                  // Table 42
     {89.95, 5e-3},             // [89.95, -45, 10000]
     {-45.0, 0.5},
     {10000.0, 0.5},
     {0.0, 0.5},  // [0, 563.643, 0]
     {563.643, 5e-4},
     {0.0, 0.5},
     {0.0, 0.5},  // [0, 0, 90]
     {90.0, 0.5}},
    {"case16",    // Table 43
     {0.0, 0.5},  // [0, -179.95, 10000]
     {-179.95, 5e-3},
     {10000.0, 0.5},
     {563.643, 5e-4},  // [563.643, 0, 0]
     {0.0, 0.5},
     {0.0, 0.5},
     {0.0, 0.5},  // [0, 0, 0]
     {0.0, 0.5}},
}};

const ChannelView& channel_named(const CaseView& c, std::string_view name) {
  for (const ChannelView& ch : c.channels) {
    if (ch.name == name) {
      return ch;
    }
  }
  ADD_FAILURE() << c.name << ": no channel " << name;
  return c.channels.front();
}

void expect_initial(const CaseView& c, std::string_view channel, double expected_si,
                    double tolerance_si) {
  const ChannelView& ch = channel_named(c, channel);
  double diff = ch.reference.front() - expected_si;
  if (ch.wrapped) {
    diff = wrap_to_pi(diff);
  }
  EXPECT_LE(std::abs(diff), tolerance_si)
      << c.name << " " << channel << ": sample-0 reference=" << ch.reference.front()
      << " Vol II=" << expected_si << " diff=" << diff << " tolerance=" << tolerance_si;
}

TEST(NescF16Band, InitialConditionsMatchVolII) {
  const std::vector<CaseView> cases = f16_cases();
  ASSERT_EQ(cases.size(), kInitialConditions.size());
  int visited_cases = 0;
  for (std::size_t i = 0; i < cases.size(); ++i) {
    const CaseView& c = cases[i];
    const VolIIInitialCondition& ic = kInitialConditions.at(i);
    ASSERT_EQ(c.name, ic.name);
    int visited_channels = 0;
    const auto angle = [&](std::string_view channel, Printed p) {
      expect_initial(c, channel, degrees_to_radians(p.value), degrees_to_radians(tolerance_of(p)));
      ++visited_channels;
    };
    const auto length = [&](std::string_view channel, Printed p) {
      expect_initial(c, channel, feet_to_meters(p.value), feet_to_meters(tolerance_of(p)));
      ++visited_channels;
    };
    angle("latitude", ic.latitude_deg);
    angle("longitude", ic.longitude_deg);
    length("altitude", ic.altitude_ft);
    length("v_north", ic.v_north_ft_s);
    length("v_east", ic.v_east_ft_s);
    length("v_down", ic.v_down_ft_s);
    angle("roll", ic.roll_deg);
    angle("yaw", ic.yaw_deg);
    EXPECT_EQ(visited_channels, kInitialConditionChannelCount) << c.name;
    ++visited_cases;
  }
  EXPECT_EQ(visited_cases, kCaseCount);
}

}  // namespace
