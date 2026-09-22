// ADR-0021 R1 (bit-identity): drives the post-change RigidBody6DofDynamics + rewritten Integrator
// through the exact sampled initial states and applied wrench the R1 golden was captured with
// (tests/golden/dynamics/rigid_body_6dof_r1_manifest.md), and asserts representation-level equality
// (std::bit_cast<std::uint64_t>, never ==) against the frozen result. Never touches the golden
// itself -- see the manifest for how/when/under what it was captured.
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <string>

#include "astra/config/config.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/mass_properties_model.hpp"
#include "astra/interfaces/wrench.hpp"
#include "astra/kernel/integrator.hpp"
#include "astra/kernel/rigid_body_6dof_dynamics.hpp"
#include "astra/kernel/state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "golden/dynamics/rigid_body_6dof_r1_golden.hpp"

namespace {

using astra::Config;
using astra::Integrator;
using astra::IntegratorKind;
using astra::PluginLoader;
using astra::RigidBody6DofDynamics;
using astra::SignalRegistry;
using astra::StateVector;
using astra::Wrench;

double from_bits(std::uint64_t value) { return std::bit_cast<double>(value); }
std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

TEST(RigidBodyR1Golden, MatchesFrozenGoldenBitwiseAcrossEverySampledInitialState) {
  const PluginLoader loader(ASTRA_TEST_PLUGIN_DIR);
  auto earth_loaded = loader.load("plugins/physical/earth/flat");
  auto gravity_loaded = loader.load("plugins/physical/gravity/constant");
  auto mass_loaded = loader.load("plugins/physical/mass/constant");

  SignalRegistry registry;
  earth_loaded.model->init(Config::from_string("", "r1-golden-test-earth"), registry);
  gravity_loaded.model->init(Config::from_string("g: 9.80665\n", "r1-golden-test-gravity"),
                             registry);
  mass_loaded.model->init(
      Config::from_string("mass: 1000.0\nixx: 1000.0\niyy: 1500.0\nizz: 2000.0\n",
                          "r1-golden-test-mass"),
      registry);

  auto* earth = dynamic_cast<astra::EarthModel*>(earth_loaded.model.get());
  auto* gravity = dynamic_cast<astra::GravityModel*>(gravity_loaded.model.get());
  auto* mass = dynamic_cast<astra::MassPropertiesModel*>(mass_loaded.model.get());
  ASSERT_NE(earth, nullptr);
  ASSERT_NE(gravity, nullptr);
  ASSERT_NE(mass, nullptr);

  RigidBody6DofDynamics rigid_body(*earth, *gravity, *mass);
  const Integrator integrator(static_cast<std::size_t>(astra::kStateSize), IntegratorKind::kRk4,
                              astra::kDefaultDt);

  Wrench wrench;
  wrench.force_b = Eigen::Vector3d{astra::test::r1::kRigidBody6DofR1Wrench[0],
                                   astra::test::r1::kRigidBody6DofR1Wrench[1],
                                   astra::test::r1::kRigidBody6DofR1Wrench[2]};
  wrench.moment_b = Eigen::Vector3d{astra::test::r1::kRigidBody6DofR1Wrench[3],
                                    astra::test::r1::kRigidBody6DofR1Wrench[4],
                                    astra::test::r1::kRigidBody6DofR1Wrench[5]};

  const auto derivative = [&](double t, const double* q, double* dq) noexcept {
    rigid_body.set_applied_wrench(wrench);
    rigid_body.derivative(t, q, dq);
  };

  for (std::size_t sample = 0; sample < astra::test::r1::kRigidBody6DofR1Initial.size(); ++sample) {
    StateVector y;
    for (int i = 0; i < astra::kStateSize; ++i) {
      y(i) =
          from_bits(astra::test::r1::kRigidBody6DofR1Initial[sample][static_cast<std::size_t>(i)]);
    }

    rigid_body.begin_frame();
    integrator.step(derivative, 0.0, y.data());
    rigid_body.post_step(y.data());

    for (int i = 0; i < astra::kStateSize; ++i) {
      EXPECT_EQ(bits(y(i)),
                astra::test::r1::kRigidBody6DofR1Result[sample][static_cast<std::size_t>(i)])
          << "sample " << sample << " component " << i;
    }
  }
}

}  // namespace
