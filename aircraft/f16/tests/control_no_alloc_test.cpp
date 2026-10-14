// The sim-side F16_control peer's exchanges allocate nothing (docs/architecture.md §14/§15; M7 PR
// 11a, docs/plans/m7-pr11a-control-peer.md, D10). It is in the F-16's no-alloc executable, beside
// no_alloc_test.cpp, which stays unchanged, as does tests/physics/no_alloc_test.cpp: a deviation
// from ADR-0029's Consequences line ("tests/physics/no_alloc_test.cpp gains a peer config"), with
// the reason that the brief keeps that file and F16NoAlloc unchanged (Joe's amendment 9).
//
// ExchangesAllocateNothing flies 13.4's tracked config, cut to 1,000 frames, with every per-frame
// branch the peer has inside them: 13.4's base point latches at the frame-0 exchange; a schedule
// entry changes all three commands at frame 300; the lateral offset applies from frame 500, so the
// cross-track distance is computed at every exchange after it. The run must complete all 1,000
// frames with no halt (no_alloc_test.cpp's pattern), and the peer's signals must show that each
// branch ran.
#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/kernel/simulation.hpp"
#include "astra/util/units.hpp"
#include "tests/support/alloc_counter.hpp"

namespace {

std::string read_text(const std::string& path) {
  std::ifstream in{path};
  if (!in) {
    throw astra::Error("cannot read " + path);
  }
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

void replace_once(std::string& text, std::string_view from, std::string_view to) {
  const std::size_t at = text.find(from);
  if (at == std::string::npos || text.find(from, at + 1) != std::string::npos) {
    throw astra::Error("the config must hold exactly one '" + std::string{from} + "'");
  }
  text.replace(at, from.size(), to);
}

TEST(F16ControlNoAlloc, ExchangesAllocateNothing) {
  std::string text =
      read_text(std::string{ASTRA_F16_CONFIG_DIR} + "/case13p4_subsonic_lateral_side_step.yaml");
  replace_once(text, "  frames: 60001\n", "  frames: 1000\n");
  replace_once(text, "    lateral_offset: {frame: 20000, offset_ft: 2000}",
               "    lateral_offset: {frame: 500, offset_ft: 2000}\n"
               "    schedule:\n"
               "      - {frame: 300, altitude_ft: 10113, equivalent_airspeed_knots: 282.98, "
               "course_deg: 60}");
  const astra::Config config = astra::Config::from_string(text, "f16-control-no-alloc");
  astra::Simulation sim = astra::Simulation::from_config(config, ASTRA_TEST_PLUGIN_DIR);
  ASSERT_EQ(sim.frames(), 1000U);

  const std::size_t before = astra_test::allocation_count();
  const astra::HaltReason halt = sim.run();
  const std::size_t after = astra_test::allocation_count();

  EXPECT_FALSE(halt) << "halted on check " << static_cast<int>(halt.check) << " at frame "
                     << halt.frame << " (" << halt.source << ")";
  EXPECT_EQ(sim.clock().frame(), 1000U) << "the run must complete, not merely allocate nothing "
                                           "while halting early";
  EXPECT_EQ(after, before) << "zero allocations over 1000 frames with the F16_control peer at "
                              "divisor 10 (docs/architecture.md §14/§15)";

  // Each branch ran: the schedule's commands are in force, and the cross-track error is live.
  const auto signal = [&sim](std::string_view name) {
    const double* value = sim.registry().find(name);
    if (value == nullptr) {
      throw astra::Error("no signal " + std::string{name});
    }
    return *value;
  };
  EXPECT_EQ(signal("transport.peer.refusal"), 0.0);
  EXPECT_EQ(signal("transport.peer.altitude_command"), astra::units::feet_to_meters(10113.0));
  EXPECT_EQ(signal("transport.peer.course_command"), astra::units::degrees_to_radians(60.0));
  EXPECT_NE(signal("transport.peer.lateral_deviation_error"), 0.0);
}

}  // namespace
