// R1-Sim (ADR-0021, end-to-end companion to R1): a full Simulation::from_config run
// over the frozen cruise_end_to_end_r1_fixture.yaml in this directory, captured from
// the 26ad594 build. Human-owned (tests/golden/**); see
// rigid_body_6dof_r1_manifest.md for how, when, and under what this was captured.
// Never hand-edit; re-run capture_r1_golden.sh under a fresh Joe authorization instead.
#pragma once

#include <array>
#include <cstdint>

namespace astra::test::r1sim {

constexpr std::array<std::uint64_t, 13> kCruiseEndToEndR1Result = {
    {0x3f03011b530ea446ULL, 0xbdac04e0e62b7f25ULL, 0x40c386f3fee09505ULL, 0x406cba8b5c680ab0ULL,
     0xbfcc8e3d73a0427cULL, 0x402328c79f8ffdedULL, 0x3fefff201d14a0b0ULL, 0xbf4eabc984a7d3d4ULL,
     0x3f8ddb904d9ff4f2ULL, 0x3f32bb57d90d3134ULL, 0xbf8fe63361a7bfb9ULL, 0x3fa903eba96bfd65ULL,
     0x3f81e6fc180cfcc0ULL}};
constexpr std::uint64_t kCruiseEndToEndR1Frames = 1000;
constexpr bool kCruiseEndToEndR1Halted = false;

}  // namespace astra::test::r1sim
