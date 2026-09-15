// core/util/frames.hpp against the ADR-0001 sign-pinning oracle.
// tests/golden/frames/adr0001_cases.hpp is human-owned (docs/process/workflow.md §Verification):
// every number in it was computed independently of this code, so these tests judge the
// implementation against the convention, not against itself.
#include "astra/util/frames.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>

#include "golden/frames/adr0001_cases.hpp"

namespace {

using astra::test::adr0001::kCases;
using astra::test::adr0001::kTolerance;

void expect_vec3_near(std::span<const double, 3> expected, const Eigen::Vector3d& actual) {
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_NEAR(expected[i], actual(static_cast<Eigen::Index>(i)), kTolerance);
  }
}

void expect_vec4_near(std::span<const double, 4> expected,
                      const astra::frames::Quaternion& actual) {
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_NEAR(expected[i], actual(static_cast<Eigen::Index>(i)), kTolerance);
  }
}

void expect_row_near(std::span<const double, 3> expected_row, const Eigen::Matrix3d& actual,
                     std::size_t row) {
  for (std::size_t col = 0; col < 3; ++col) {
    EXPECT_NEAR(expected_row[col],
                actual(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(col)), kTolerance);
  }
}

TEST(Frames, QuaternionFromEulerMatchesAdr0001) {
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.name);
    const astra::frames::EulerAngles euler{c.euler_nb[0], c.euler_nb[1], c.euler_nb[2]};
    expect_vec4_near(c.q_nb, astra::frames::quaternion_from_euler(euler));
  }
}

TEST(Frames, DcmFromEulerMatchesAdr0001) {
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.name);
    const astra::frames::EulerAngles euler{c.euler_nb[0], c.euler_nb[1], c.euler_nb[2]};
    const Eigen::Matrix3d dcm = astra::frames::dcm_from_euler(euler);
    for (std::size_t r = 0; r < 3; ++r) {
      expect_row_near(c.C_nb[r], dcm, r);
    }
  }
}

TEST(Frames, DcmFromQuaternionMatchesAdr0001) {
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.name);
    const astra::frames::Quaternion q_nb{c.q_nb[0], c.q_nb[1], c.q_nb[2], c.q_nb[3]};
    const Eigen::Matrix3d dcm = astra::frames::dcm_from_quaternion(q_nb);
    for (std::size_t r = 0; r < 3; ++r) {
      expect_row_near(c.C_nb[r], dcm, r);
    }
  }
}

TEST(Frames, DcmFromQuaternionAgreesWithDcmFromEulerForEveryCase) {
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.name);
    const astra::frames::EulerAngles euler{c.euler_nb[0], c.euler_nb[1], c.euler_nb[2]};
    const astra::frames::Quaternion q_nb{c.q_nb[0], c.q_nb[1], c.q_nb[2], c.q_nb[3]};
    const Eigen::Matrix3d from_euler = astra::frames::dcm_from_euler(euler);
    const Eigen::Matrix3d from_quat = astra::frames::dcm_from_quaternion(q_nb);
    for (Eigen::Index r = 0; r < 3; ++r) {
      for (Eigen::Index col = 0; col < 3; ++col) {
        EXPECT_NEAR(from_euler(r, col), from_quat(r, col), kTolerance);
      }
    }
  }
}

TEST(Frames, EulerFromQuaternionRoundTripsWhereNotGimbalLocked) {
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.name);
    const astra::frames::Quaternion q_nb{c.q_nb[0], c.q_nb[1], c.q_nb[2], c.q_nb[3]};
    const astra::frames::EulerAngles euler = astra::frames::euler_from_quaternion(q_nb);
    // pitch_90 is gimbal lock (psi, phi not unique): only theta is pinned there.
    EXPECT_NEAR(c.euler_nb[1], euler(1), kTolerance);
    if (c.euler_roundtrip) {
      EXPECT_NEAR(c.euler_nb[0], euler(0), kTolerance);
      EXPECT_NEAR(c.euler_nb[2], euler(2), kTolerance);
    }
  }
}

TEST(Frames, RotateNToBMatchesNorthEastDownExpressedInBody) {
  const std::array<Eigen::Vector3d, 3> axes{Eigen::Vector3d::UnitX(), Eigen::Vector3d::UnitY(),
                                            Eigen::Vector3d::UnitZ()};
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.name);
    const astra::frames::Quaternion q_nb{c.q_nb[0], c.q_nb[1], c.q_nb[2], c.q_nb[3]};
    for (std::size_t i = 0; i < 3; ++i) {
      expect_vec3_near(c.n_axes_in_b[i], astra::frames::rotate_n_to_b(q_nb, axes[i]));
    }
  }
}

TEST(Frames, RotateBToNMatchesBodyAxesExpressedInNav) {
  const std::array<Eigen::Vector3d, 3> axes{Eigen::Vector3d::UnitX(), Eigen::Vector3d::UnitY(),
                                            Eigen::Vector3d::UnitZ()};
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.name);
    const astra::frames::Quaternion q_nb{c.q_nb[0], c.q_nb[1], c.q_nb[2], c.q_nb[3]};
    for (std::size_t i = 0; i < 3; ++i) {
      expect_vec3_near(c.b_axes_in_n[i], astra::frames::rotate_b_to_n(q_nb, axes[i]));
    }
  }
}

TEST(Frames, QuaternionDerivativeMatchesAdr0001Kinematics) {
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.name);
    const astra::frames::Quaternion q_nb{c.q_nb[0], c.q_nb[1], c.q_nb[2], c.q_nb[3]};
    const Eigen::Vector3d omega_b{c.omega_b[0], c.omega_b[1], c.omega_b[2]};
    expect_vec4_near(c.qdot_nb, astra::frames::quaternion_derivative(q_nb, omega_b));
  }
}

TEST(Frames, EigenQuaternionConversionIsTheTransposeAdr0001Warns) {
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.name);
    const astra::frames::Quaternion q_nb{c.q_nb[0], c.q_nb[1], c.q_nb[2], c.q_nb[3]};
    const Eigen::Matrix3d c_nb = astra::frames::dcm_from_quaternion(q_nb);
    const Eigen::Matrix3d via_eigen = astra::frames::to_eigen_quaternion(q_nb).toRotationMatrix();
    for (Eigen::Index r = 0; r < 3; ++r) {
      for (Eigen::Index col = 0; col < 3; ++col) {
        EXPECT_NEAR(c_nb(r, col), via_eigen(col, r), kTolerance);
      }
    }
    const astra::frames::Quaternion round_trip =
        astra::frames::from_eigen_quaternion(astra::frames::to_eigen_quaternion(q_nb));
    expect_vec4_near(c.q_nb, round_trip);
  }
}

}  // namespace
