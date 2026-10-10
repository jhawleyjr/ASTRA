// plugins/physical/mass/constant's product-of-inertia convention (M7 PR 4,
// docs/plans/m7-pr4-mass-convention.md): ixy/ixz/iyz are S&L products of inertia, negated into the
// tensor. Loads the real plugin through PluginLoader, the same shape as
// notional_transport_mass_test.cpp -- no reaching into the plugin's anonymous-namespace internals.
// Each config sits at physical.mass, so every refusal names the path the simulator reports.
//
// The two-point-mass test is not a re-derivation of the plugin's own arithmetic: it builds the
// expected tensor from the definition I = sum(m * (|r|^2 E - r r^T)), never from a negation, and
// checks the physical sign of the plugin's xz entry.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/mass_properties_model.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::MassPropertiesModel;
using astra::MassProps;

constexpr std::string_view kOrigin = "constant-mass-fixture";

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

// Runs the plugin's init() on `fields`, the body of physical.mass's flow mapping.
MassProps load(std::string_view fields) {
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  const astra::LoadedPlugin loaded = loader.load("plugins/physical/mass/constant");
  auto& mass = dynamic_cast<MassPropertiesModel&>(*loaded.model);
  astra::SignalRegistry registry;
  const Config config =
      Config::from_string("physical:\n  mass: { " + std::string{fields} + " }\n", kOrigin);
  mass.init(config.at("physical.mass"), registry);
  return mass.get();
}

void expect_bitwise_equal(const Eigen::Matrix3d& actual, const Eigen::Matrix3d& expected) {
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      EXPECT_EQ(bits(actual(row, col)), bits(expected(row, col)))
          << "entry (" << row << ", " << col << "): " << actual(row, col) << " vs "
          << expected(row, col);
    }
  }
}

TEST(ConstantMass, PositiveProductsAreNegatedIntoTheTensorBitwise) {
  const MassProps props =
      load("mass: 5.0, ixx: 10.0, iyy: 11.0, izz: 12.0, ixy: 1.25, ixz: 2.5, iyz: 0.75");
  Eigen::Matrix3d expected;
  // clang-format off
  expected << 10.0,  -1.25, -2.5,
              -1.25, 11.0,  -0.75,
              -2.5,  -0.75, 12.0;
  // clang-format on
  expect_bitwise_equal(props.inertia_b, expected);
}

// The R1 golden's own mass config (rigid_body_r1_golden_test.cpp): with no product key the tensor
// keeps the bits it had before M7 PR 4, +0.0 off the diagonal, not the -0.0 that -p would give.
TEST(ConstantMass, OmittedProductsLeaveThePrePr4Tensor) {
  const MassProps props = load("mass: 1000.0, ixx: 1000.0, iyy: 1500.0, izz: 2000.0");
  Eigen::Matrix3d expected = Eigen::Matrix3d::Zero();
  expected.diagonal() << 1000.0, 1500.0, 2000.0;
  expect_bitwise_equal(props.inertia_b, expected);
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      if (row != col) {
        EXPECT_FALSE(std::signbit(props.inertia_b(row, col))) << row << ", " << col;
      }
    }
  }
}

// Two equal point masses at (+a, 0, +c) and (-a, 0, -c), a = 1.5 m and c = 0.5 m, symmetric about
// the CG: integral(x*z*dm) = 2*m*a*c > 0, so the tensor's xz entry must be negative. The pair alone
// lies on a line through the CG, a singular tensor the plugin refuses; a centered body of 1 kg*m^2
// about each axis, with no products, makes it positive-definite. Every value is exact in binary.
TEST(ConstantMass, TwoPointMassesGiveAPositiveProductAndANegativeTensorEntry) {
  constexpr double kPointMass = 2.0;  // kg
  const std::array<Eigen::Vector3d, 2> points{Eigen::Vector3d{1.5, 0.0, 0.5},
                                              Eigen::Vector3d{-1.5, 0.0, -0.5}};
  Eigen::Matrix3d expected = Eigen::Matrix3d::Identity();  // the centered body
  double product_xz = 0.0;                                 // integral(x*z*dm)
  for (const Eigen::Vector3d& r : points) {
    expected += kPointMass * (r.squaredNorm() * Eigen::Matrix3d::Identity() - r * r.transpose());
    product_xz += kPointMass * r.x() * r.z();
  }
  ASSERT_GT(product_xz, 0.0);
  EXPECT_EQ(product_xz, 3.0);
  EXPECT_EQ(expected(0, 2), -3.0);

  const MassProps props =
      load(std::format("mass: 4.0, ixx: {}, iyy: {}, izz: {}, ixz: {}", expected(0, 0),
                       expected(1, 1), expected(2, 2), product_xz));
  EXPECT_LT(props.inertia_b(0, 2), 0.0);
  EXPECT_LT(props.inertia_b(2, 0), 0.0);
  expect_bitwise_equal(props.inertia_b, expected);
}

// simulation_physical_config_test.cpp's ixy = 2 case under the new meaning, and its mirror: the
// tensor's eigenvalues are 3, 1 and -1 for either sign, so both are refused with today's message.
TEST(ConstantMass, StillRefusesANonPositiveDefiniteTensorForEitherSign) {
  for (const std::string_view ixy : {"2.0", "-2.0"}) {
    try {
      static_cast<void>(load("mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0, ixy: " + std::string{ixy}));
      ADD_FAILURE() << "expected ConfigError for ixy " << ixy;
    } catch (const ConfigError& error) {
      EXPECT_EQ(error.config_path(), "physical.mass") << "ixy " << ixy;
      EXPECT_STREQ(error.what(),
                   "config 'constant-mass-fixture' at 'physical.mass': inertia tensor (ixx, iyy, "
                   "izz, ixy, ixz, iyz) must be positive-definite")
          << "ixy " << ixy;
    }
  }
}

TEST(ConstantMass, StillRefusesANonFiniteProductNamingTheKey) {
  try {
    static_cast<void>(load("mass: 10.0, ixx: 1.0, iyy: 1.0, izz: 1.0, ixz: .nan"));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "physical.mass.ixz");
    EXPECT_STREQ(error.what(),
                 "config 'constant-mass-fixture' at 'physical.mass.ixz': ixz must be finite");
  }
}

}  // namespace
