// M7b PR 4's heading invariance and its wrap control (docs/plans/m7b-pr4-f16-symmetry.md, sections
// 9 and 10; Q12, R13 and R14), registered only in a Release build (section 12). One test flies
// every run once (amendment 7): the base run at psi0 = 0, the perturbation set P's eight, the
// rotated runs at 0.6 rad and 172.5 deg, and the unwrapped-track-error control, 12 runs in all, on
// the flat Earth with gravity/constant and ISA-1976 in still air, the shipped aero, and the
// earth_rate_none peer (section 9).
//
// The bound (kind: measured sensitivity yardstick, ADR-0034 Decision 4): for each channel c and
// record k, |d_c(k)| <= F Y_c(k), with d_c the rotated run's value minus the base run's, and Y_c(k)
// the largest |x_p,c(s) - x_base,c(s)| over p in P and every record s <= k (decision 2's running
// maximum). F = 2^24 and P are section 9's, fixed before any run and never adjusted after one.
//
// The relation is the reference (section 3): the horizontal position rotated back by -psi0, and the
// wrap of psi - psi0, both in long double with cosl, sinl and remainderl. It never calls frames::,
// icd_pack::, law:: or a plugin. The wrap control's replay precondition does call law::, as PR 3's
// N-03 replay did: it is a precondition on the control's registration, not the reference.
//
// Physics label: the stop hook's `just test-unit` never runs this.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/state.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/frames.hpp"
#include "astra/util/units.hpp"
#include "f16_symmetry.hpp"
#include "icd/astra_icd.h"

namespace {

namespace sym = astra::test::nesc::f16_symmetry;
namespace law = sym::law;
namespace units = astra::units;
using Real = long double;

static_assert(std::numeric_limits<Real>::digits == 64,
              "section 3: the relation is evaluated with a 64-bit significand");

constexpr Real kF = 16777216.0L;   // section 9: F = 2^24
constexpr Real kOrders = 1000.0L;  // section 10: the wrap control's 10^3
constexpr double kZeroDelta =
    0x1p-52;  // section 9, decision 2: ulp(1) for an exactly zero component
const Real kTwoPi = 2.0L * std::numbers::pi_v<Real>;

double parsed(std::string_view text) {
  double value = 0.0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    throw astra::Error("f16_symmetry_heading: not a number: " + std::string{text});
  }
  return value;
}

// --- The compared quantities (section 9), in long double at every record ------------------------

constexpr std::size_t kStateChannels = 9;
constexpr std::array<std::string_view, kStateChannels> kStateChannelNames = {
    "u", "v", "w", "p", "q", "r", "N rotated back", "E rotated back", "altitude (-D)"};
constexpr std::size_t kNorth = 6;
constexpr std::size_t kEast = 7;
constexpr std::size_t kRequestChannels = 6;
constexpr std::array<std::string_view, kRequestChannels> kRequestChannelNames = {
    "phi", "theta", "alpha", "beta", "altitude", "psi - psi0 wrapped"};
constexpr std::size_t kWrappedPsi = 5;
constexpr std::size_t kReplyChannels = sym::kReplyDoubles;

template <std::size_t C>
using Rows = std::vector<std::array<Real, C>>;

struct Channels {
  Rows<kStateChannels> states;
  Rows<kRequestChannels> requests;
  Rows<kReplyChannels> replies;
};

Real wide(double x) { return static_cast<Real>(x); }

// One run's channels, with its heading psi0: v_b and omega_b from the state stream; the horizontal
// position rotated back by -psi0 and the altitude -D; phi, theta, alpha, beta and the altitude from
// the requests, and psi - psi0 wrapped by remainderl into [-pi, pi]; and the five reply fields.
Channels channels_of(const sym::Records& run, Real psi0) {
  const Real c = std::cos(psi0);
  const Real s = std::sin(psi0);
  Channels out;
  out.states.reserve(run.states.size());
  for (const astra::RecordedState& r : run.states) {
    const auto& y = r.y;
    const Real north = wide(y[0]);
    const Real east = wide(y[1]);
    out.states.push_back({wide(y[3]), wide(y[4]), wide(y[5]), wide(y[10]), wide(y[11]), wide(y[12]),
                          c * north + s * east, -s * north + c * east, -wide(y[2])});
  }
  out.requests.reserve(run.requests.size());
  for (const astra_sim_to_sw_v1& q : run.requests) {
    out.requests.push_back({wide(q.phi), wide(q.theta), wide(q.alpha), wide(q.beta),
                            wide(q.altitude), std::remainder(wide(q.psi) - psi0, kTwoPi)});
  }
  out.replies.reserve(run.replies.size());
  for (const astra_sw_to_sim_v1& p : run.replies) {
    out.replies.push_back({wide(p.elevator), wide(p.aileron), wide(p.rudder), wide(p.throttle[0]),
                           wide(p.throttle[1])});
  }
  return out;
}

// --- The yardstick ------------------------------------------------------------------------------

template <std::size_t C>
void widen(Rows<C>& largest, const Rows<C>& run, const Rows<C>& base) {
  largest.resize(base.size());
  for (std::size_t k = 0; k < base.size(); ++k) {
    for (std::size_t c = 0; c < C; ++c) {
      largest[k][c] = std::max(largest[k][c], std::abs(run[k][c] - base[k][c]));
    }
  }
}

template <std::size_t C>
void running_max(Rows<C>& y) {
  for (std::size_t k = 1; k < y.size(); ++k) {
    for (std::size_t c = 0; c < C; ++c) {
      y[k][c] = std::max(y[k][c], y[k - 1][c]);
    }
  }
}

struct Ratio {
  Real worst = 0.0L;  // max |d| / (F Y)
  std::size_t at = 0;
  Real d = 0.0L;
  Real y = 0.0L;
  Real largest_y = 0.0L;
};

template <std::size_t C>
std::array<Ratio, C> ratios(const Rows<C>& run, const Rows<C>& base, const Rows<C>& y,
                            std::size_t from = 0) {
  std::array<Ratio, C> out{};
  for (std::size_t k = 0; k < base.size(); ++k) {
    for (std::size_t c = 0; c < C; ++c) {
      out[c].largest_y = std::max(out[c].largest_y, y[k][c]);
      if (k < from) {
        continue;
      }
      const Real d = run[k][c] - base[k][c];
      const Real bound = kF * y[k][c];
      Real ratio = 0.0L;  // d == 0 meets any bound, Y == 0 included
      if (d != 0.0L) {
        ratio = bound == 0.0L ? std::numeric_limits<Real>::infinity() : std::abs(d) / bound;
      }
      if (ratio > out[c].worst) {
        out[c] = {ratio, k, d, y[k][c], out[c].largest_y};
      }
    }
  }
  return out;
}

template <std::size_t C>
void print_ratios(std::string_view name, std::string_view stream, const std::array<Ratio, C>& r,
                  const std::array<std::string_view, C>& names) {
  for (std::size_t c = 0; c < C; ++c) {
    std::cout << std::format(
        "{}: {} {}: max |d|/(F Y) {:.3e} at record {} (d {:+.3e}, Y {:.3e}); largest Y {:.3e}\n",
        name, stream, names[c], static_cast<double>(r[c].worst), r[c].at,
        static_cast<double>(r[c].d), static_cast<double>(r[c].y),
        static_cast<double>(r[c].largest_y));
  }
}

template <std::size_t C>
bool within(const std::array<Ratio, C>& r) {
  return std::ranges::all_of(r, [](const Ratio& x) { return x.worst <= 1.0L; });
}

// Every heading run's own checks: no halt, the registered header, and section 9's guards (P-04).
void check_run(std::string_view name, const sym::Records& run) {
  EXPECT_FALSE(run.halt) << name << ": halted, " << run.halt_source << " at frame "
                         << run.halt.frame;
  if (const std::optional<std::string> v = sym::header_violation(run, sym::kCourseStepFrames)) {
    ADD_FAILURE() << name << ": " << *v;
  }
  if (const std::optional<std::string> v = sym::heading_guard_violation(run)) {
    ADD_FAILURE() << name
                  << ": left section 9's guards (this stops the PR; it is not a miss): " << *v;
  }
}

// --- The wrap control's replay (section 10, P-03; Joe's third decision, point 4) --------------

struct Replay {
  std::optional<std::string> precondition_miss;
  std::optional<std::size_t> k_star;    // the first exchange whose two phi_cmd differ
  std::optional<std::size_t> k_dagger;  // the first exchange whose two replies differ
  std::vector<std::string> fields;      // the reply fields that differ at k_dagger
};

// An exact replay of the two laws on the nominal 172.5 deg run's recorded requests. The shipped law
// runs through inputs_from(., kNone), evaluate with the peer's own Modes, Trim and References, and
// to_icd; its trim, references and commands come from a second instance of the shipped peer class,
// from the run's own transport text, published each request in turn (section 8 (d)'s route). The
// unwrapped law is the variant's substitution (section 2), written again here. The precondition:
// the shipped law's replay equals the recorded reply by value, in all five fields, at every
// exchange, before k-dagger is read.
Replay replay_wrap(const sym::Records& nominal, const std::string& text) {
  sym::peer::Options options;
  options.earth_rate = law::EarthRate::kNone;
  sym::peer::NescControlPeer peer{options};
  astra::SignalRegistry registry;
  peer.init(astra::Config::from_string(text, "f16-symmetry wrap replay").at("transport"), registry);
  Replay out;
  for (std::size_t k = 0; k < nominal.requests.size(); ++k) {
    const astra_sim_to_sw_v1& request = nominal.requests[k];
    peer.publish(request, static_cast<std::uint32_t>(request.frame));
    const law::Commands& cmd = peer.commands();
    const law::Inputs in = law::inputs_from(request, law::EarthRate::kNone);
    const law::IcdCommands shipped =
        law::to_icd(law::evaluate(in, cmd, peer.trim(), law::Modes{}, peer.references()));
    const std::array<double, sym::kReplyDoubles> replayed = {
        shipped.elevator, shipped.aileron, shipped.rudder, shipped.throttle, shipped.throttle};
    const std::array<double, sym::kReplyDoubles> recorded = sym::reply_values(nominal.replies[k]);
    for (std::size_t i = 0; i < sym::kReplyDoubles; ++i) {
      if (replayed[i] != recorded[i]) {
        out.precondition_miss =
            std::format("exchange {} {}: replayed {} recorded {}", k, sym::kReplyNames[i],
                        sym::hex(replayed[i]), sym::hex(recorded[i]));
        return out;
      }
    }
    std::array<double, sym::kReplyDoubles> unwrapped = replayed;
    if (std::isfinite(shipped.aileron)) {
      const double chi_est = in.beta + in.psi;
      const double delta_chi_cmd =
          law::saturate(law::kLateralOffsetGain * cmd.lateral_deviation_error, -law::kDeltaChiLimit,
                        law::kDeltaChiLimit);
      const double x = chi_est - (delta_chi_cmd + cmd.course);
      if (std::abs(x) > law::kHalfTurn) {
        if (!out.k_star &&
            std::abs(law::kTrackErrorGain * law::wrap_track_error(x)) < law::kPhiLimit) {
          out.k_star = k;
        }
        law::Commands substitute = cmd;
        substitute.course = chi_est - (x > 0.0 ? 1.0 : -1.0);
        substitute.lateral_deviation_error = 0.0;
        const law::IcdCommands icd = law::to_icd(
            law::evaluate(in, substitute, peer.trim(), law::Modes{}, peer.references()));
        unwrapped[1] = icd.aileron;
        unwrapped[2] = icd.rudder;
      }
    }
    if (!out.k_dagger) {
      for (std::size_t i = 0; i < sym::kReplyDoubles; ++i) {
        if (unwrapped[i] != replayed[i]) {
          out.fields.emplace_back(sym::kReplyNames[i]);
        }
      }
      if (!out.fields.empty()) {
        out.k_dagger = k;
      }
    }
  }
  return out;
}

std::vector<std::string> sorted(std::vector<std::string> fields) {
  std::ranges::sort(fields);
  return fields;
}

TEST(F16SymmetryHeading, ThreeHeadingsAndTheUnwrappedControl) {
  const std::array<sym::Heading, 3> headings = sym::headings();
  const sym::Heading& base_heading = headings[0];
  const std::string base_text = sym::heading_text(
      "earth_rate_none", base_heading.psi0, base_heading.course_deg, base_heading.step_course_deg);

  // The one-time injections inside F's estimate (section 9): each course text's conversion against
  // the intended double, psi0 and psi0 + 15 deg.
  for (const sym::Heading& h : headings) {
    const Real psi0 = wide(parsed(h.psi0));
    const Real course = wide(units::degrees_to_radians(parsed(h.course_deg)));
    const Real step = wide(units::degrees_to_radians(parsed(h.step_course_deg)));
    std::cout << std::format(
        "{}: psi0 {}, course {} deg, step {} deg; course - psi0 {:+.3e} rad, step - (psi0 + 15 "
        "deg) "
        "{:+.3e} rad\n",
        h.name, h.psi0, h.course_deg, h.step_course_deg, static_cast<double>(course - psi0),
        static_cast<double>(step - (psi0 + wide(units::degrees_to_radians(15.0)))));
  }

  // The base run's q0 is the config route's own frame-0 q_nb, which
  // F16SymmetryTrim.TheConfigRoutesQuaternionIsTheTrims pins bitwise; it is written back through
  // the same route as P's (amendment 5).
  const astra::frames::Quaternion q0 = [&] {
    astra::Simulation sim = sym::build(base_text, "f16-symmetry base q0");
    return astra::frames::Quaternion{
        sim.vehicles()[0].state().segment<astra::state::kQuaternionSize>(
            astra::state::kQuaternion)};
  }();
  const sym::Records base = sym::fly_with_quaternion(base_text, "heading base", q0);
  check_run("heading base", base);
  const Channels base_channels = channels_of(base, 0.0L);

  // P (section 9; decision 2): each component of q0 moved by +delta_i and by -delta_i, with delta_i
  // = ulp(|q_i|) for a nonzero component and 2^-52 for x and z, which are exactly zero. Flown once
  // each; Y is the running maximum of their divergence from the base.
  Rows<kStateChannels> y_states;
  Rows<kRequestChannels> y_requests;
  Rows<kReplyChannels> y_replies;
  for (int i = 0; i < 4; ++i) {
    const auto index = static_cast<Eigen::Index>(i);
    const double magnitude = std::abs(q0[index]);
    const double delta =
        magnitude == 0.0
            ? kZeroDelta
            : std::nextafter(magnitude, std::numeric_limits<double>::infinity()) - magnitude;
    for (const double sign : {1.0, -1.0}) {
      astra::frames::Quaternion q = q0;
      q[index] += sign * delta;
      const std::string name =
          std::format("P: q[{}] {}{}", i, sign > 0.0 ? "+" : "-", sym::hex(delta));
      const sym::Records run = sym::fly_with_quaternion(base_text, name, q);
      check_run(name, run);
      const Channels channels = channels_of(run, 0.0L);
      widen(y_states, channels.states, base_channels.states);
      widen(y_requests, channels.requests, base_channels.requests);
      widen(y_replies, channels.replies, base_channels.replies);
    }
  }
  running_max(y_states);
  running_max(y_requests);
  running_max(y_replies);

  // The two rotated runs, q0 from the config (euler_nb [psi0, theta, 0]): the route by which only
  // the initial quaternion's heading changed.
  std::optional<sym::Records> nominal_172;
  std::string nominal_172_text;
  for (std::size_t h = 1; h < headings.size(); ++h) {
    const sym::Heading& heading = headings[h];
    const std::string text = sym::heading_text("earth_rate_none", heading.psi0, heading.course_deg,
                                               heading.step_course_deg);
    sym::Records run = sym::fly(text, heading.name);
    check_run(heading.name, run);
    const Channels channels = channels_of(run, wide(parsed(heading.psi0)));
    const auto states = ratios(channels.states, base_channels.states, y_states);
    const auto requests = ratios(channels.requests, base_channels.requests, y_requests);
    const auto replies = ratios(channels.replies, base_channels.replies, y_replies);
    print_ratios(heading.name, "state", states, kStateChannelNames);
    print_ratios(heading.name, "request", requests, kRequestChannelNames);
    print_ratios(heading.name, "reply", replies, sym::kReplyNames);
    EXPECT_TRUE(within(states) && within(requests) && within(replies))
        << heading.name << ": |d| must stay within F Y at every record (section 9)";
    if (h == 2) {
      nominal_172 = std::move(run);
      nominal_172_text = text;
    }
  }
  if (!nominal_172) {
    FAIL() << "the 172.5 deg run was not flown";
    return;
  }

  // The wrap control (section 10, P-03): the replay's precondition first, then k-dagger, then the
  // control's first differing record from the nominal 172.5 deg run, bitwise, and the failure by
  // orders of magnitude after it.
  const Replay replay = replay_wrap(*nominal_172, nominal_172_text);
  if (replay.precondition_miss) {
    FAIL() << "the wrap control's replay precondition missed; it goes to Joe: "
           << *replay.precondition_miss;
    return;
  }
  if (!replay.k_dagger || !replay.k_star) {
    FAIL() << "the two laws never differ before the run ends: the control has not failed as "
              "registered (k* found: "
           << replay.k_star.has_value() << ")";
    return;
  }
  const std::size_t k_dagger = *replay.k_dagger;
  const std::size_t k_star = *replay.k_star;
  std::cout << std::format(
      "the unwrapped track error: k* = exchange {} (frame {}), k-dagger = exchange {} (frame {}), "
      "the replay's fields:",
      k_star, k_star * sym::kDivisor, k_dagger, k_dagger * sym::kDivisor);
  for (const std::string& f : replay.fields) {
    std::cout << " " << f;
  }
  std::cout << "\n";
  EXPECT_GE(k_dagger, k_star) << "section 10's derivation puts k-dagger at or after k*";

  const sym::Heading& wrap_heading = headings[2];
  const std::string control_text =
      sym::heading_text("earth_rate_none_unwrapped_track_error", wrap_heading.psi0,
                        wrap_heading.course_deg, wrap_heading.step_course_deg);
  const sym::Records control = sym::fly(control_text, "the unwrapped track error");
  check_run("the unwrapped track error", control);
  const std::optional<sym::Mismatch> first =
      sym::first_mismatch(control, *nominal_172, sym::Compare::kBitwise);
  if (!first) {
    FAIL() << "the control must differ from the nominal run";
    return;
  }
  std::cout << std::format(
      "the unwrapped track error: first record differing bitwise from the nominal 172.5 deg run: "
      "{}\n",
      first->describe());
  EXPECT_EQ(first->stream, "reply");
  EXPECT_EQ(first->index, k_dagger);
  EXPECT_EQ(sorted(first->fields), sorted(replay.fields));

  const Channels control_channels = channels_of(control, wide(parsed(wrap_heading.psi0)));
  const std::size_t after_request = k_dagger + 1;
  const std::size_t after_state = k_dagger * sym::kDivisor + 1;
  const auto states = ratios(control_channels.states, base_channels.states, y_states, after_state);
  const auto requests =
      ratios(control_channels.requests, base_channels.requests, y_requests, after_request);
  print_ratios("the unwrapped track error", "state", states, kStateChannelNames);
  print_ratios("the unwrapped track error", "request", requests, kRequestChannelNames);
  EXPECT_GE(requests[kWrappedPsi].worst, kOrders) << "psi - psi0 wrapped, after k-dagger";
  EXPECT_GE(states[kNorth].worst, kOrders) << "N rotated back, after k-dagger";
  EXPECT_GE(states[kEast].worst, kOrders) << "E rotated back, after k-dagger";
}

}  // namespace
