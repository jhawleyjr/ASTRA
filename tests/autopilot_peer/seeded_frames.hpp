// The seeded frame sequences of M7b's oracle (b)'s unit-level equivalence
// (docs/plans/m7b-pr3-autopilot-peer.md §8 item 3; every constant here is pre-registered there).
// Each seed draws its setpoints and trims, then frames whose fields are wide enough to saturate
// every output, with held frames, non-finite fields and every dt edge mixed in. Randomness is
// std::mt19937_64 with an in-house uniform (AGENTS.md: the standard distributions are not
// portable). Shared by autopilot_peer_equivalence_test.cpp (R5) and autopilot_peer_test.cpp (the
// Transport).
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <span>

#include "icd/astra_icd.h"

namespace autopilot_peer_test {

inline constexpr std::array<std::uint64_t, 8> kSeeds = {1, 2, 3, 4, 5, 6, 7, 8};  // §8 item 3
inline constexpr std::size_t kFramesPerSeed = 50000;                              // §8 item 3
inline constexpr std::size_t kSegment = 1000;                                     // §8 item 3
inline constexpr double kPeriod = 0.01;  // s, the exchange period at divisor 10

// §8 item 3's in-house uniform on [0, 1): the top 53 bits of one draw.
class Uniform {
 public:
  explicit Uniform(std::uint64_t seed) : engine_(seed) {}
  double operator()() { return static_cast<double>(engine_() >> 11U) * 0x1.0p-53; }
  double in(double lo, double hi) { return lo + ((hi - lo) * (*this)()); }
  std::size_t index(std::size_t n) {
    return std::min(static_cast<std::size_t>((*this)() * static_cast<double>(n)), n - 1);
  }

 private:
  std::mt19937_64 engine_;
};

// The law's parameters, drawn per seed.
struct SeedParameters {
  double altitude;
  double airspeed;
  double heading;
  double elevator_trim;
  double throttle_trim;
  double theta_trim;
};

enum class DtKind : std::uint8_t { kPeriod, kRepeat, kBackward, kGap, kOneSecond, kRandom, kNan };
inline constexpr std::size_t kDtKinds = 7;

using Field = double astra_sim_to_sw_v1::*;
// The nine fields the law reads (embedded/autopilot/autopilot.cpp:35-36), and the ones it does not.
inline constexpr std::array<Field, 9> kReadFields = {
    &astra_sim_to_sw_v1::altitude, &astra_sim_to_sw_v1::vertical_speed,
    &astra_sim_to_sw_v1::theta,    &astra_sim_to_sw_v1::q,
    &astra_sim_to_sw_v1::psi,      &astra_sim_to_sw_v1::phi,
    &astra_sim_to_sw_v1::p,        &astra_sim_to_sw_v1::tas,
    &astra_sim_to_sw_v1::r};
inline constexpr std::array<Field, 12> kUnreadFields = {
    &astra_sim_to_sw_v1::alpha, &astra_sim_to_sw_v1::beta, &astra_sim_to_sw_v1::u,
    &astra_sim_to_sw_v1::v,     &astra_sim_to_sw_v1::w,    &astra_sim_to_sw_v1::mach,
    &astra_sim_to_sw_v1::cas,   &astra_sim_to_sw_v1::lat,  &astra_sim_to_sw_v1::lon,
    &astra_sim_to_sw_v1::ax,    &astra_sim_to_sw_v1::ay,   &astra_sim_to_sw_v1::az};
inline constexpr std::array<double, 3> kNonFinite = {std::numeric_limits<double>::quiet_NaN(),
                                                     std::numeric_limits<double>::infinity(),
                                                     -std::numeric_limits<double>::infinity()};

// Byte equality of two replies, as the protocol copies them (tests/embedded/autopilot_test.cpp's
// same_bytes).
inline bool same_bytes(const astra_sw_to_sim_v1& a, const astra_sw_to_sim_v1& b) {
  return std::ranges::equal(std::as_bytes(std::span{&a, 1}), std::as_bytes(std::span{&b, 1}));
}

class SeededFrames {
 public:
  explicit SeededFrames(std::uint64_t seed) : u_(seed) {
    parameters_.altitude = u_.in(0.0, 15000.0);
    parameters_.airspeed = u_.in(50.0, 300.0);
    parameters_.heading = u_.in(-std::numbers::pi, std::numbers::pi);
    parameters_.elevator_trim = u_.in(-0.3, 0.3);
    parameters_.throttle_trim = u_.in(0.0, 1.0);
    parameters_.theta_trim = u_.in(-0.5, 0.5);
    if (seed == 4) {  // §8 item 3: one seed in four puts a trim exactly on a limit
      parameters_.elevator_trim = -0.3;
      parameters_.throttle_trim = 1.0;
    } else if (seed == 8) {
      parameters_.elevator_trim = 0.3;
      parameters_.throttle_trim = 0.0;
    }
  }

  [[nodiscard]] const SeedParameters& parameters() const { return parameters_; }
  [[nodiscard]] DtKind last_dt_kind() const { return last_dt_; }
  [[nodiscard]] bool has_dt_kind() const { return index_ > 1; }

  // The next frame. Its t advances by one of §8's Δt kinds from a finite clock; a NaN frame leaves
  // the clock where it was.
  astra_sim_to_sw_v1 next() {
    constexpr std::array<double, 3> kScales = {1.0, 100.0, 5000.0};  // §8 item 3
    if (index_ % kSegment == 0) {
      scale_ = kScales[u_.index(kScales.size())];
    }
    double t = clock_;
    if (index_ > 0) {
      last_dt_ = draw_dt();
      switch (last_dt_) {
        case DtKind::kPeriod:
          clock_ += kPeriod;
          break;
        case DtKind::kRepeat:
          break;
        case DtKind::kBackward:
          clock_ -= kPeriod;
          break;
        case DtKind::kGap:
          clock_ += 1.5;
          break;
        case DtKind::kOneSecond:
          clock_ += 1.0;
          break;
        case DtKind::kRandom:
          clock_ += 2.0 * u_();
          break;
        case DtKind::kNan:
          break;
      }
      t = last_dt_ == DtKind::kNan ? std::numeric_limits<double>::quiet_NaN() : clock_;
    }

    astra_sim_to_sw_v1 in{};
    in.magic = ASTRA_ICD_MAGIC;
    in.version = ASTRA_ICD_VERSION;
    in.frame = index_ * 10;
    in.t = t;
    in.altitude = parameters_.altitude + ((u_() - 0.5) * 2.0 * scale_);
    in.tas = parameters_.airspeed + ((u_() - 0.5) * 2.0 * 60.0);
    in.psi = u_.in(-4.0 * std::numbers::pi, 4.0 * std::numbers::pi);
    in.phi = u_.in(-1.0, 1.0);
    in.theta = u_.in(-1.0, 1.0);
    in.p = u_.in(-2.0, 2.0);
    in.q = u_.in(-2.0, 2.0);
    in.r = u_.in(-2.0, 2.0);
    in.vertical_speed = u_.in(-200.0, 200.0);
    for (const Field field : kUnreadFields) {
      in.*field = u_.in(-100.0, 100.0);
    }
    if (u_() < 0.05) {  // §8 item 3
      in.status_flags |= ASTRA_ICD_STATUS_WIND_INVALID;
    }
    for (const std::uint32_t bit :
         {ASTRA_ICD_STATUS_ATMOSPHERE_INVALID, ASTRA_ICD_STATUS_CAS_SUPERSONIC,
          ASTRA_ICD_STATUS_ACCEL_INVALID, ASTRA_ICD_STATUS_LATLON_NOT_MODELED}) {
      if (u_() < 0.5) {
        in.status_flags |= bit;
      }
    }
    if (u_() < 0.03) {  // §8 item 3
      in.*kReadFields[u_.index(kReadFields.size())] = kNonFinite[u_.index(kNonFinite.size())];
    }
    if (u_() < 0.05) {  // §8 item 3
      in.*kUnreadFields[u_.index(kUnreadFields.size())] = kNonFinite[u_.index(kNonFinite.size())];
    }
    ++index_;
    return in;
  }

 private:
  // §8 item 3: 0.01 s 0.85; 0 0.03; -0.01 s 0.03; 1.5 s 0.02; 1.0 s 0.02; u*2 s 0.04; NaN 0.01.
  DtKind draw_dt() {
    const double x = u_();
    if (x < 0.85) {
      return DtKind::kPeriod;
    }
    if (x < 0.88) {
      return DtKind::kRepeat;
    }
    if (x < 0.91) {
      return DtKind::kBackward;
    }
    if (x < 0.93) {
      return DtKind::kGap;
    }
    if (x < 0.95) {
      return DtKind::kOneSecond;
    }
    if (x < 0.99) {
      return DtKind::kRandom;
    }
    return DtKind::kNan;
  }

  Uniform u_;
  SeedParameters parameters_{};
  double clock_ = 0.0;
  double scale_ = 1.0;
  std::uint64_t index_ = 0;
  DtKind last_dt_ = DtKind::kPeriod;
};

}  // namespace autopilot_peer_test
