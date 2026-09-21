// VehicleState (core/kernel/vehicle_state.hpp): named accessors read the right slice of a
// StateVector per ADR-0003's layout, and each returns by value (no dangling Eigen expression).
#include "astra/kernel/vehicle_state.hpp"

#include <gtest/gtest.h>

namespace {

TEST(VehicleState, AccessorsReadTheirAdr0003Slice) {
  astra::StateVector y;
  y << 1.0, 2.0, 3.0,      // p_n
      4.0, 5.0, 6.0,       // v_b
      0.5, 0.5, 0.5, 0.5,  // q_nb
      7.0, 8.0, 9.0;       // omega_b
  const astra::VehicleState vs{y};

  EXPECT_EQ(vs.p_n(), (Eigen::Vector3d{1.0, 2.0, 3.0}));
  EXPECT_EQ(vs.v_b(), (Eigen::Vector3d{4.0, 5.0, 6.0}));
  EXPECT_EQ(vs.q_nb(), (astra::frames::Quaternion{0.5, 0.5, 0.5, 0.5}));
  EXPECT_EQ(vs.omega_b(), (Eigen::Vector3d{7.0, 8.0, 9.0}));
  // ADR-0021: vector() returns an Eigen::Map by value (a view, not an owning copy), retyped from a
  // `const StateVector&` reference so it can also view a slice of a runtime-sized buffer -- address
  // identity with y was never a contract this class's own header promised ("read-only, non-owning
  // view", not "identical object"), so this checks value equality instead.
  EXPECT_EQ(vs.vector(), y);
}

}  // namespace
