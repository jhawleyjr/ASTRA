// The ICD layout under clang-18 C++23 (ADR-0006). The C11 side is tests/icd/icd_c11.c, compiled by
// ctest under gcc and clang with -std=c11. Both pin every offset, not just the total size.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "icd/astra_icd.h"

static_assert(ASTRA_ICD_VERSION == 1U);
static_assert(ASTRA_ICD_MAGIC == 0x41535452U);

static_assert(sizeof(astra_sim_to_sw_v1) == 200);
static_assert(alignof(astra_sim_to_sw_v1) == 8);
static_assert(std::is_standard_layout_v<astra_sim_to_sw_v1>);
static_assert(std::is_trivially_copyable_v<astra_sim_to_sw_v1>);
static_assert(offsetof(astra_sim_to_sw_v1, magic) == 0);
static_assert(offsetof(astra_sim_to_sw_v1, version) == 4);
static_assert(offsetof(astra_sim_to_sw_v1, frame) == 8);
static_assert(offsetof(astra_sim_to_sw_v1, t) == 16);
static_assert(offsetof(astra_sim_to_sw_v1, phi) == 24);
static_assert(offsetof(astra_sim_to_sw_v1, theta) == 32);
static_assert(offsetof(astra_sim_to_sw_v1, psi) == 40);
static_assert(offsetof(astra_sim_to_sw_v1, p) == 48);
static_assert(offsetof(astra_sim_to_sw_v1, q) == 56);
static_assert(offsetof(astra_sim_to_sw_v1, r) == 64);
static_assert(offsetof(astra_sim_to_sw_v1, u) == 72);
static_assert(offsetof(astra_sim_to_sw_v1, v) == 80);
static_assert(offsetof(astra_sim_to_sw_v1, w) == 88);
static_assert(offsetof(astra_sim_to_sw_v1, alpha) == 96);
static_assert(offsetof(astra_sim_to_sw_v1, beta) == 104);
static_assert(offsetof(astra_sim_to_sw_v1, tas) == 112);
static_assert(offsetof(astra_sim_to_sw_v1, cas) == 120);
static_assert(offsetof(astra_sim_to_sw_v1, mach) == 128);
static_assert(offsetof(astra_sim_to_sw_v1, altitude) == 136);
static_assert(offsetof(astra_sim_to_sw_v1, vertical_speed) == 144);
static_assert(offsetof(astra_sim_to_sw_v1, lat) == 152);
static_assert(offsetof(astra_sim_to_sw_v1, lon) == 160);
static_assert(offsetof(astra_sim_to_sw_v1, ax) == 168);
static_assert(offsetof(astra_sim_to_sw_v1, ay) == 176);
static_assert(offsetof(astra_sim_to_sw_v1, az) == 184);
static_assert(offsetof(astra_sim_to_sw_v1, status_flags) == 192);
static_assert(offsetof(astra_sim_to_sw_v1, reserved) == 196);

static_assert(sizeof(astra_sw_to_sim_v1) == 64);
static_assert(alignof(astra_sw_to_sim_v1) == 8);
static_assert(std::is_standard_layout_v<astra_sw_to_sim_v1>);
static_assert(std::is_trivially_copyable_v<astra_sw_to_sim_v1>);
static_assert(offsetof(astra_sw_to_sim_v1, magic) == 0);
static_assert(offsetof(astra_sw_to_sim_v1, version) == 4);
static_assert(offsetof(astra_sw_to_sim_v1, frame) == 8);
static_assert(offsetof(astra_sw_to_sim_v1, elevator) == 16);
static_assert(offsetof(astra_sw_to_sim_v1, aileron) == 24);
static_assert(offsetof(astra_sw_to_sim_v1, rudder) == 32);
static_assert(offsetof(astra_sw_to_sim_v1, throttle) == 40);
static_assert(sizeof(astra_sw_to_sim_v1::throttle) == 16);
static_assert(offsetof(astra_sw_to_sim_v1, mode_flags) == 56);
static_assert(offsetof(astra_sw_to_sim_v1, fault_flags) == 60);

namespace {

TEST(Icd, SizesAndMagicHoldAtRunTime) {
  EXPECT_EQ(sizeof(astra_sim_to_sw_v1), 200U);
  EXPECT_EQ(sizeof(astra_sw_to_sim_v1), 64U);
  EXPECT_EQ(ASTRA_ICD_VERSION, 1U);
  EXPECT_EQ(static_cast<char>((ASTRA_ICD_MAGIC >> 24U) & 0xFFU), 'A');
  EXPECT_EQ(static_cast<char>((ASTRA_ICD_MAGIC >> 16U) & 0xFFU), 'S');
  EXPECT_EQ(static_cast<char>((ASTRA_ICD_MAGIC >> 8U) & 0xFFU), 'T');
  EXPECT_EQ(static_cast<char>(ASTRA_ICD_MAGIC & 0xFFU), 'R');
}

}  // namespace
