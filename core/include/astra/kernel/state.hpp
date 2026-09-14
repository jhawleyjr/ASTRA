// The state vector: 13 doubles in a fixed layout (ADR-0003). Types and indices only; the EOM and
// the integrators that operate on it arrive with M1.
#pragma once

#include <Eigen/Core>

namespace astra {

inline constexpr int kStateSize = 13;

// p_n(3) v_b(3) q_nb(4) omega_b(3). The Earth model owns the meaning of p_n (NED metres for flat
// Earth; latitude, longitude in rad and altitude in m for round and WGS-84). q_nb rotates n to b,
// Hamilton, scalar-first (w, x, y, z), renormalized after every integrator step (ADR-0001). Mass is
// not state in v1.
using StateVector = Eigen::Vector<double, kStateSize>;

namespace state {

inline constexpr int kPosition = 0;      // p_n, 3
inline constexpr int kVelocity = 3;      // v_b = (u, v, w), 3
inline constexpr int kQuaternion = 6;    // q_nb = (w, x, y, z), 4
inline constexpr int kAngularRate = 10;  // omega_b = (p, q, r), 3

inline constexpr int kPositionSize = 3;
inline constexpr int kVelocitySize = 3;
inline constexpr int kQuaternionSize = 4;
inline constexpr int kAngularRateSize = 3;

static_assert(kPosition + kPositionSize == kVelocity);
static_assert(kVelocity + kVelocitySize == kQuaternion);
static_assert(kQuaternion + kQuaternionSize == kAngularRate);
static_assert(kAngularRate + kAngularRateSize == kStateSize);

}  // namespace state

}  // namespace astra
