// Regression for a Codex/Joe-found One Definition Rule hazard (docs/plans/
// pr4-notional-aero-turbofan.md): aircraft/notional_transport/physical/aero/aero_math.hpp and
// tests/plugins/aero_math_odr_check/aero_math.hpp both declare an Inputs/Coefficients/
// compute_wrench() triple, in their own distinct namespaces
// (astra::aircraft_notional_transport_aero_math / astra::odr_check_aero_math). Under
// ASTRA_STATIC_PLUGINS every plugin's object links into one binary with no per-plugin visibility
// boundary (unlike the shared-object presets' own -fvisibility=hidden/-fvisibility-inlines-hidden);
// if these two headers ever shared one namespace (the bug this fixture and test exist to catch --
// aircraft/_template's own copy originally did, before it was given its own
// astra::aircraft_template_aero_math namespace and new-aircraft.sh was taught to rewrite it on
// copy), the linker would silently keep only one compute_wrench() definition for both plugins.
// Loading both in one process and checking each produces its own distinct, correct answer proves
// that is not happening -- this test runs under all four `just check` presets, static-plugins
// included, with no special preset targeting needed.
#include <gtest/gtest.h>

#include <string>

#include "astra/config/config.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/kernel/environment_sample.hpp"
#include "astra/kernel/state.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::AeroModel;
using astra::Config;
using astra::EnvironmentSample;
using astra::InputRegistry;
using astra::SignalRegistry;
using astra::StateVector;
using astra::VehicleState;

TEST(AeroMathNamespaceIsolation, TwoAircraftSpecificMathHeadersDoNotCollideUnderStaticLinking) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};

  astra::LoadedPlugin real_loaded = loader.load("aircraft/notional_transport/physical/aero");
  auto* const real_aero = dynamic_cast<AeroModel*>(real_loaded.model.get());
  const Config real_config = Config::from_file(std::string{ASTRA_AIRCRAFT_DIR} +
                                               "/notional_transport/configs/cruise.yaml");
  SignalRegistry real_registry;
  real_aero->init(real_config.at("vehicle.models").at(0), real_registry);
  InputRegistry real_inputs;
  real_aero->bind_inputs(real_inputs, real_registry);
  real_inputs.write("controls.elevator", 0.0);
  real_inputs.write("controls.aileron", 0.0);
  real_inputs.write("controls.rudder", 0.0);
  EXPECT_TRUE(real_inputs.latch().empty());

  StateVector y = StateVector::Zero();
  y(astra::state::kQuaternion) = 1.0;  // identity q_nb
  y.segment<astra::state::kVelocitySize>(astra::state::kVelocity) =
      Eigen::Vector3d(230.0, 0.0, 0.0);
  const VehicleState vs{y};
  EnvironmentSample env;
  env.atmosphere.density_kg_m3 = 0.4128;

  const astra::Wrench real_wrench = real_aero->wrench_b(vs, env);

  astra::LoadedPlugin fixture_loaded = loader.load("tests/plugins/aero_math_odr_check");
  auto* const fixture_aero = dynamic_cast<AeroModel*>(fixture_loaded.model.get());
  SignalRegistry fixture_registry;
  fixture_aero->init(Config::from_string("{}\n", "odr-check-fixture"), fixture_registry);
  const astra::Wrench fixture_wrench = fixture_aero->wrench_b(vs, env);

  // The fixture's own fixed, input-independent sentinel -- proves its own compute_wrench() ran,
  // not one merged with the real plugin's.
  EXPECT_DOUBLE_EQ(fixture_wrench.force_b(0), 1.0);
  EXPECT_DOUBLE_EQ(fixture_wrench.force_b(1), 2.0);
  EXPECT_DOUBLE_EQ(fixture_wrench.force_b(2), 3.0);
  EXPECT_DOUBLE_EQ(fixture_wrench.moment_b(0), 4.0);
  EXPECT_DOUBLE_EQ(fixture_wrench.moment_b(1), 5.0);
  EXPECT_DOUBLE_EQ(fixture_wrench.moment_b(2), 6.0);

  // The real plugin's own physics-based answer -- large-magnitude and finite, nothing like the
  // fixture's small fixed sentinel; a collision (either direction) would make these match instead.
  EXPECT_TRUE(real_wrench.force_b.allFinite());
  EXPECT_GT(real_wrench.force_b.norm(), 1000.0);
  EXPECT_NE(real_wrench.force_b(0), fixture_wrench.force_b(0));
}

}  // namespace
