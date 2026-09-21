// aircraft/_template/hardware/propulsion/turbofan (docs/plans/pr4-notional-aero-turbofan.md, R9):
// the un-edited template's own config (aircraft/_template/configs/cruise.yaml) carries
// thrust_ref_n/ density_ref_kgm3 zeroed and TODO-marked. Pins the intended failure mode: copying
// _template and never touching the numbers cannot silently fly. Mirrors
// tests/unit/mass/template_mass_test.cpp.
#include <gtest/gtest.h>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/propulsion_model.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::PropulsionModel;

TEST(TemplateTurbofan, ZeroedConfigThrowsConfigErrorNamingTheTodoField) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load("aircraft/_template/hardware/propulsion/turbofan");
  auto* const turbofan = dynamic_cast<PropulsionModel*>(loaded.model.get());
  astra::SignalRegistry registry;

  try {
    // aircraft/_template/configs/cruise.yaml's own un-edited values (left station).
    turbofan->init(Config::from_string("station: left\n"
                                       "x_offset_from_cg_m: 0.0\n"
                                       "y_offset_from_cg_m: 0.0\n"
                                       "z_offset_from_cg_m: 0.0\n"
                                       "thrust_ref_n: 0.0\n"
                                       "density_ref_kgm3: 0.0\n",
                                       "template-turbofan-fixture"),
                   registry);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "thrust_ref_n");
  }
}

// Codex general-review finding: `value > 0.0` alone is true for +Inf, so an infinite
// density_ref_kgm3 used to pass checked_positive() and then silently produce zero thrust
// (finite_rho / Inf == 0) -- a plausible-looking, wrong answer, not a loud failure. Fixed:
// checked_positive() now also requires std::isfinite().
TEST(TemplateTurbofan, InfiniteDensityRefThrowsConfigErrorInsteadOfSilentlyZeroingThrust) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load("aircraft/_template/hardware/propulsion/turbofan");
  auto* const turbofan = dynamic_cast<PropulsionModel*>(loaded.model.get());
  astra::SignalRegistry registry;

  try {
    turbofan->init(Config::from_string("station: left\n"
                                       "x_offset_from_cg_m: 0.0\n"
                                       "y_offset_from_cg_m: 0.0\n"
                                       "z_offset_from_cg_m: 0.0\n"
                                       "thrust_ref_n: 29878.121867743088\n"
                                       "density_ref_kgm3: .inf\n",
                                       "template-turbofan-infinite-density-fixture"),
                   registry);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "density_ref_kgm3");
  }
}

}  // namespace
