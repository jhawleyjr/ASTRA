// aircraft/notional_transport/physical/mass (PLAN.md M3 PR 1): loads the real plugin through
// PluginLoader, the same shape as tests/unit/gravity/inverse_square_test.cpp -- no reaching into
// the plugin's anonymous-namespace internals. Numbers are O1's transcription of JSBSim's "737"
// example aircraft (docs/refs/aircrafts/737.xml); see aircraft/notional_transport/configs/
// cruise.yaml for the full citation and the ixz sign derivation.
//
// The principal-axis-tilt test is not a re-derivation of the plugin's own arithmetic: it
// eigendecomposes the tensor init() actually produced and checks the physical invariant the sign
// convention exists to get right. This specific transcription gives ~0.34 deg NOSE-UP -- what the
// source's own ixz token gives, transcribed faithfully, not a sign error to "fix" toward some
// assumed nose-down value (Joe, 2026-09-19).
#include <gtest/gtest.h>

#include <Eigen/Eigenvalues>
#include <cmath>
#include <numbers>

#include "astra/config/config.hpp"
#include "astra/interfaces/mass_properties_model.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::MassPropertiesModel;
using astra::MassProps;

MassProps load() {
  static astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  static astra::LoadedPlugin loaded;
  loaded = loader.load("aircraft/notional_transport/physical/mass");
  auto* const mass = dynamic_cast<MassPropertiesModel*>(loaded.model.get());
  astra::SignalRegistry registry;
  // O1's transcription (docs/refs/aircrafts/737.xml, cited in cruise.yaml): ixz below is the S&L
  // product of inertia integral(x*z*dm), not the tensor entry -- the plugin negates it.
  mass->init(Config::from_string("mass: 37648.0\n"
                                 "ixx: 761970.0\n"
                                 "iyy: 1997120.0\n"
                                 "izz: 2567919.0\n"
                                 "ixz: -10846.5\n",
                                 "notional-transport-mass-fixture"),
             registry);
  return mass->get();
}

TEST(NotionalTransportMass, MassAndDiagonalInertiaMatchTheTranscribedSource) {
  const MassProps props = load();
  EXPECT_DOUBLE_EQ(props.mass, 37648.0);
  EXPECT_DOUBLE_EQ(props.inertia_b(0, 0), 761970.0);
  EXPECT_DOUBLE_EQ(props.inertia_b(1, 1), 1997120.0);
  EXPECT_DOUBLE_EQ(props.inertia_b(2, 2), 2567919.0);
}

TEST(NotionalTransportMass, ConfigIxzIsTheNegatedTensorEntry) {
  const MassProps props = load();
  // Config ixz (-10846.5) is integral(x*z*dm); the tensor entry is its negation, +10846.5.
  EXPECT_DOUBLE_EQ(props.inertia_b(0, 2), 10846.5);
  EXPECT_DOUBLE_EQ(props.inertia_b(2, 0), 10846.5);
}

TEST(NotionalTransportMass, PrincipalAxisTiltMatchesTheTranscribedSourceNotAnAssumedValue) {
  const MassProps props = load();
  const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eigen(props.inertia_b);
  ASSERT_EQ(eigen.info(), Eigen::Success);

  // Eigenvalues ascend; Ixx (761970) is the smallest of the three, so index 0 is the eigenvector
  // nearest the body x-axis -- the principal x-axis this transcription actually produces.
  Eigen::Vector3d axis = eigen.eigenvectors().col(0);
  if (axis.x() < 0.0) {
    axis = -axis;  // eigenvector sign is arbitrary; keep it near +x, not -x
  }

  // Tilt from the body x-axis toward z (FRD: +z is down), so positive means nose-down. y is
  // exactly zero here (Ixy = Iyz = 0), so this is a pure x-z rotation.
  EXPECT_NEAR(axis.y(), 0.0, 1e-12);
  const double tilt_deg = std::atan2(axis.z(), axis.x()) * 180.0 / std::numbers::pi;
  // ~0.34 deg NOSE-UP (negative), from the source's own negated_crossproduct_inertia="true"
  // <ixz>8000</ixz> SLUG*FT2 token -- not the nose-down tilt a generic textbook ratio would give.
  EXPECT_NEAR(tilt_deg, -0.3441009605, 1e-6);
}

}  // namespace
