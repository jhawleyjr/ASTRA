// M7b PR 4's meridian-mirror flights (docs/plans/m7b-pr4-f16-symmetry.md, sections 7, 8 and 10;
// R11, R12 and R14), registered only in a Release build (section 12). R flies to the right of the
// meridian and L to the left, in one process, on one build and one worktree state; neither writes a
// log.
// - F16SymmetryMirror.Case13p{3,4}VariantPair (section 7; by value): Omega and the four signed-beta
//   tables removed, so every record of all three streams satisfies its mask.
// - F16SymmetryShipped.Case13p{3,4}Pair (section 8; by value, under its dated deviation from the
//   M7b plan's amendment 1 and C-05's resolution): parts (a) to (c) at every RK4 stage operand and
//   (d) and (e) at every exchange, after the stage rebuild's replay precondition, and the
//   trajectory report, which is not a bound.
// - F16SymmetryControl.* (section 10): each control fails on its registered object, as registered.
//
// A nominal miss is a defect until shown otherwise, and stops the PR; a departure from the
// signed-zero guard stops it too, as a departure, not a miss; a rebuild miss goes to Joe as a
// test-side question. Physics label: the stop hook's `just test-unit` never runs these.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "f16_symmetry.hpp"

namespace {

namespace sym = astra::test::nesc::f16_symmetry;

// One pair: both runs, each with the text it flew.
struct Pair {
  sym::Shape right_shape;
  sym::Shape left_shape;
  std::string right_text;
  std::string left_text;
  sym::Records right;
  sym::Records left;
};

Pair fly_pair(std::string_view name, std::string_view variant, std::string_view aero,
              bool side_step) {
  Pair pair;
  pair.right_shape =
      side_step ? sym::side_step(sym::Side::kRight) : sym::course_step(sym::Side::kRight);
  pair.left_shape =
      side_step ? sym::side_step(sym::Side::kLeft) : sym::course_step(sym::Side::kLeft);
  pair.right_text = sym::mirror_text(variant, aero, pair.right_shape);
  pair.left_text = sym::mirror_text(variant, aero, pair.left_shape);
  pair.right = sym::fly(pair.right_text, std::format("{} R", name));
  pair.left = sym::fly(pair.left_text, std::format("{} L", name));
  return pair;
}

// Each run's own checks: no halt, the registered header (section 7, P-02), and the signed-zero
// guard (section 7, scoped by P-04).
void check_run(std::string_view name, const sym::Records& run, std::uint64_t frames) {
  EXPECT_FALSE(run.halt) << name << ": halted, " << run.halt_source << " at frame "
                         << run.halt.frame;
  if (const std::optional<std::string> v = sym::header_violation(run, frames)) {
    ADD_FAILURE() << name << ": " << *v;
  }
  if (const std::optional<std::string> v = sym::mirror_envelope_violation(run)) {
    ADD_FAILURE() << name
                  << ": left the signed-zero guard (section 7; this stops the PR, and it is "
                  << "not a miss): " << *v;
  }
}

void check_pair(std::string_view name, const Pair& pair) {
  check_run(std::format("{} R", name), pair.right, pair.right_shape.frames);
  check_run(std::format("{} L", name), pair.left, pair.left_shape.frames);
}

// Section 5's prefix, in both runs of a pair flown with Omega = 0.
void check_prefix(std::string_view name, const Pair& pair) {
  for (const auto* run : {&pair.right, &pair.left}) {
    if (const std::optional<std::string> v = sym::prefix_violation(*run, pair.right_shape.step)) {
      ADD_FAILURE() << name << ": the pre-step prefix is not lateral-zero (section 5): " << *v;
    }
  }
}

void print_first(std::string_view name, const std::optional<sym::Mismatch>& first) {
  std::cout << std::format("{}: first differing record under the mask: {}\n", name,
                           first ? first->describe() : std::string{"none"});
}

// Section 8's setup for one run: its own aero and the |beta| aero, each without a transport, and
// the text it flew.
sym::PartsSetup setup_for(std::string_view aero, const sym::Shape& shape, const std::string& text) {
  const std::string frames = std::to_string(shape.frames);
  return {sym::round_earth_text(aero, frames),
          sym::round_earth_text(sym::variant_aero("abs_beta_tables"), frames), text};
}

// --- Section 7: the variant pairs ----------------------------------------------------------------

void variant_pair(std::string_view name, bool side_step) {
  const Pair pair =
      fly_pair(name, "earth_rate_none", sym::variant_aero("abs_beta_tables"), side_step);
  check_pair(name, pair);
  check_prefix(name, pair);
  const std::optional<sym::Mismatch> first =
      sym::first_mismatch(pair.right, pair.left, sym::Compare::kMirror);
  print_first(name, first);
  std::cout << std::format("{}: {} states, {} exchanges per run\n", name, pair.right.states.size(),
                           pair.right.requests.size());
  EXPECT_FALSE(first) << name << ": every record of all three streams must satisfy its mask";
}

TEST(F16SymmetryMirror, Case13p3VariantPair) { variant_pair("13.3 variant pair", false); }

TEST(F16SymmetryMirror, Case13p4VariantPair) { variant_pair("13.4 variant pair", true); }

// --- Section 8: the shipped pairs ----------------------------------------------------------------

void shipped_pair(std::string_view name, bool side_step) {
  const Pair pair = fly_pair(name, "default_options", sym::kShippedAero, side_step);
  check_pair(name, pair);  // the N-01 exclusion: no prefix check under Omega
  const std::uint64_t frames = pair.right_shape.frames;
  const std::uint64_t exchanges = (frames + sym::kDivisor - 1) / sym::kDivisor;
  for (const bool right : {true, false}) {
    const std::string run_name = std::format("{} {}", name, right ? "R" : "L");
    const sym::Records& run = right ? pair.right : pair.left;
    const sym::PartsSetup setup =
        setup_for(sym::kShippedAero, right ? pair.right_shape : pair.left_shape,
                  right ? pair.right_text : pair.left_text);
    const sym::PartsReport report = sym::read_parts(run, setup);
    std::cout << std::format("{}: {}\n", run_name, sym::describe(report));
    if (report.rebuild_miss) {
      FAIL() << run_name << ": the stage rebuild missed, a test-side question for Joe (section 8): "
             << *report.rebuild_miss;
      return;
    }
    EXPECT_EQ(report.failure_count, 0U)
        << run_name << ": a part failed, an asymmetry outside the inventory (section 8)";
    EXPECT_EQ(report.operands, 4 * frames + 1) << run_name;
    EXPECT_EQ(report.exchanges, exchanges) << run_name;
  }
  print_first(name, sym::first_mismatch(pair.right, pair.left, sym::Compare::kMirror));
  sym::print_trajectory_report(name, pair.right, pair.left);
}

TEST(F16SymmetryShipped, Case13p3Pair) { shipped_pair("13.3 shipped pair", false); }

TEST(F16SymmetryShipped, Case13p4Pair) { shipped_pair("13.4 shipped pair", true); }

// --- Section 10: the controls --------------------------------------------------------------------

std::vector<std::string> sorted(std::vector<std::string> fields) {
  std::ranges::sort(fields);
  return fields;
}

TEST(F16SymmetryControl, OmegaOnly) {
  // The bitwise mirror, from engagement: the first differing record is reply 0, in exactly
  // {aileron, rudder}, so state 0 and request 0 are equal under the mask.
  constexpr std::string_view kName = "Omega-only control";
  const Pair pair = fly_pair(kName, "default_options", sym::variant_aero("abs_beta_tables"), false);
  check_pair(kName, pair);
  const std::optional<sym::Mismatch> first =
      sym::first_mismatch(pair.right, pair.left, sym::Compare::kMirror);
  print_first(kName, first);
  if (!first) {
    FAIL() << kName << " must fail the bitwise mirror";
    return;
  }
  EXPECT_EQ(first->stream, "reply");
  EXPECT_EQ(first->index, 0U);
  EXPECT_EQ(sorted(first->fields), (std::vector<std::string>{"aileron", "rudder"}));
}

// The tables only and absCl0's sign dropped: section 5's prefix is exactly lateral-zero in both
// runs, no record differs at or before the step, and the first differing record is a state with
// frame in (S, S + 10], or the reply at S + 10, with state p and r expected first (amendment 2).
void first_interval_control(std::string_view name, std::string_view aero) {
  const Pair pair = fly_pair(name, "earth_rate_none", aero, false);
  check_pair(name, pair);
  check_prefix(name, pair);
  const std::optional<sym::Mismatch> first =
      sym::first_mismatch(pair.right, pair.left, sym::Compare::kMirror);
  print_first(name, first);
  if (!first) {
    FAIL() << name << " must fail the bitwise mirror";
    return;
  }
  const std::uint64_t step = pair.right_shape.step;
  const bool in_state_window =
      first->stream == "state" && first->frame > step && first->frame <= step + sym::kDivisor;
  const bool at_reply = first->stream == "reply" && first->frame == step + sym::kDivisor;
  EXPECT_TRUE(in_state_window || at_reply)
      << name << ": the first difference must fall in the first exchange interval after the step";
  if (in_state_window) {
    const std::vector<std::string>& f = first->fields;
    EXPECT_TRUE(std::ranges::count(f, "p") == 1 && std::ranges::count(f, "r") == 1)
        << name << ": state p and r are expected first";
  }
}

TEST(F16SymmetryControl, TablesOnly) {
  first_interval_control("tables-only control", sym::kShippedAero);
}

TEST(F16SymmetryControl, Cl0SignDropped) {
  first_interval_control("sign-drop control", sym::variant_aero("abs_beta_tables_cl0_unsigned"));
}

// The doubled asymmetry (C-05 under section 8's deviation; re-registered by P-01): in each run,
// under section 8's read order, the first evaluation failing any part is part (c) at frame 0's
// stage-2 operand, in p_dot, r_dot or both. State 0 and exchange 0 pass every part. Through state
// 1, (a) and (b) hold at every operand, and (d) and (e) at exchange 0. State 1 is the first
// recorded state at which (c) fails. The pass reads through state 1, after the rebuild of frame 0.
void doubled_control(std::string_view name, bool side_step) {
  const std::string aero = sym::variant_aero("signed_tables_doubled_odd");
  const Pair pair = fly_pair(name, "default_options", aero, side_step);
  check_pair(name, pair);
  for (const bool right : {true, false}) {
    const std::string run_name = std::format("{} {}", name, right ? "R" : "L");
    const sym::PartsSetup setup = setup_for(aero, right ? pair.right_shape : pair.left_shape,
                                            right ? pair.right_text : pair.left_text);
    const sym::PartsReport report = sym::read_parts(right ? pair.right : pair.left, setup, 1);
    std::cout << std::format("{}: {}\n", run_name, sym::describe(report));
    if (report.rebuild_miss) {
      FAIL() << run_name << ": the stage rebuild missed, a test-side question for Joe (section 8): "
             << *report.rebuild_miss;
      return;
    }
    ASSERT_FALSE(report.failures.empty()) << run_name << " must fail part (c)";
    const sym::PartFailure& first = report.failures.front();
    EXPECT_EQ(first.where, "frame 0's operand 2") << run_name;
    EXPECT_EQ(first.part, 'c') << run_name;
    EXPECT_TRUE(!first.fields.empty() &&
                std::ranges::all_of(first.fields,
                                    [](const auto& f) { return f == "p_dot" || f == "r_dot"; }))
        << run_name << ": part (c) must fail in p_dot or r_dot only";
    bool state_1_fails_c = false;
    for (const sym::PartFailure& f : report.failures) {
      EXPECT_EQ(f.part, 'c') << run_name << ": part (" << f.part << ") failed at " << f.where;
      EXPECT_NE(f.where, "state 0") << run_name;
      state_1_fails_c = state_1_fails_c || (f.where == "state 1" && f.part == 'c');
    }
    EXPECT_TRUE(state_1_fails_c) << run_name << ": state 1 must fail part (c)";
    EXPECT_EQ(report.exchanges, 1U) << run_name;
  }
  sym::print_trajectory_report(name, pair.right, pair.left);
}

TEST(F16SymmetryControl, DoubledAsymmetryCase13p3) {
  doubled_control("13.3 doubled control", false);
}

TEST(F16SymmetryControl, DoubledAsymmetryCase13p4) {
  doubled_control("13.4 doubled control", true);
}

}  // namespace
