// aircraft/_template/physical/aero (docs/plans/pr4-notional-aero-turbofan.md, R9): the un-edited
// template's own config (aircraft/_template/configs/cruise.yaml) carries every coefficient zeroed
// and TODO-marked. This pins the intended failure mode: copying _template and never touching the
// numbers cannot silently fly -- init() throws a ConfigError naming the exact field still at its
// TODO value (wing_area_m2, checked first). Mirrors tests/unit/mass/template_mass_test.cpp.
#include <gtest/gtest.h>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::AeroModel;
using astra::Config;
using astra::ConfigError;

TEST(TemplateAero, ZeroedConfigThrowsConfigErrorNamingTheTodoField) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load("aircraft/_template/physical/aero");
  auto* const aero = dynamic_cast<AeroModel*>(loaded.model.get());
  astra::SignalRegistry registry;

  try {
    // aircraft/_template/configs/cruise.yaml's own un-edited values.
    aero->init(Config::from_string("wing_area_m2: 0.0\n"
                                   "wingspan_m: 0.0\n"
                                   "mean_chord_m: 0.0\n"
                                   "r_aero_b_x_m: 0.0\n"
                                   "r_aero_b_y_m: 0.0\n"
                                   "r_aero_b_z_m: 0.0\n"
                                   "cl_0: 0.0\n"
                                   "cl_alpha_per_rad: 0.0\n"
                                   "cl_elevator_per_rad: 0.0\n"
                                   "cd_0: 0.0\n"
                                   "cd_k: 0.0\n"
                                   "cy_beta_per_rad: 0.0\n"
                                   "croll_beta_per_rad: 0.0\n"
                                   "croll_p_per_rad: 0.0\n"
                                   "croll_r_per_rad: 0.0\n"
                                   "croll_aileron_per_rad: 0.0\n"
                                   "croll_rudder_per_rad: 0.0\n"
                                   "cm_alpha_per_rad: 0.0\n"
                                   "cm_q_per_rad: 0.0\n"
                                   "cm_elevator_per_rad: 0.0\n"
                                   "cn_beta_per_rad: 0.0\n"
                                   "cn_r_per_rad: 0.0\n"
                                   "cn_rudder_per_rad: 0.0\n",
                                   "template-aero-fixture"),
               registry);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "wing_area_m2");
  }
}

// Codex general-review finding (turbofan.cpp's density_ref_kgm3, same helper here): `value > 0.0`
// alone is true for +Inf, so checked_positive() used to accept an infinite wing_area_m2. Fixed:
// checked_positive() now also requires std::isfinite().
TEST(TemplateAero, InfiniteWingAreaThrowsConfigError) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load("aircraft/_template/physical/aero");
  auto* const aero = dynamic_cast<AeroModel*>(loaded.model.get());
  astra::SignalRegistry registry;

  try {
    aero->init(Config::from_string("wing_area_m2: .inf\n"
                                   "wingspan_m: 1.0\n"
                                   "mean_chord_m: 1.0\n"
                                   "r_aero_b_x_m: 0.0\n"
                                   "r_aero_b_y_m: 0.0\n"
                                   "r_aero_b_z_m: 0.0\n"
                                   "cl_0: 0.0\n"
                                   "cl_alpha_per_rad: 0.0\n"
                                   "cl_elevator_per_rad: 0.0\n"
                                   "cd_0: 0.0\n"
                                   "cd_k: 0.0\n"
                                   "cy_beta_per_rad: 0.0\n"
                                   "croll_beta_per_rad: 0.0\n"
                                   "croll_p_per_rad: 0.0\n"
                                   "croll_r_per_rad: 0.0\n"
                                   "croll_aileron_per_rad: 0.0\n"
                                   "croll_rudder_per_rad: 0.0\n"
                                   "cm_alpha_per_rad: 0.0\n"
                                   "cm_q_per_rad: 0.0\n"
                                   "cm_elevator_per_rad: 0.0\n"
                                   "cn_beta_per_rad: 0.0\n"
                                   "cn_r_per_rad: 0.0\n"
                                   "cn_rudder_per_rad: 0.0\n",
                                   "template-aero-infinite-wing-area-fixture"),
               registry);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "wing_area_m2");
  }
}

}  // namespace
