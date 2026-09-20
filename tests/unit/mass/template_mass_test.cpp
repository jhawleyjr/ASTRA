// aircraft/_template/physical/mass (PLAN.md M3 PR 1): the un-edited template's own config
// (aircraft/_template/configs/cruise.yaml) carries mass/ixx/iyy/izz all zeroed and TODO-marked.
// This pins the intended failure mode: copying _template and never touching the numbers cannot
// silently run -- init() throws a ConfigError naming the exact field still at its TODO value.
#include <gtest/gtest.h>

#include "astra/config/config.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/mass_properties_model.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"

namespace {

using astra::Config;
using astra::ConfigError;
using astra::MassPropertiesModel;

TEST(TemplateMass, ZeroedConfigThrowsConfigErrorNamingTheTodoField) {
  astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded = loader.load("aircraft/_template/physical/mass");
  auto* const mass = dynamic_cast<MassPropertiesModel*>(loaded.model.get());
  astra::SignalRegistry registry;

  try {
    // aircraft/_template/configs/cruise.yaml's own un-edited values.
    mass->init(Config::from_string("mass: 0.0\n"
                                   "ixx: 0.0\n"
                                   "iyy: 0.0\n"
                                   "izz: 0.0\n",
                                   "template-mass-fixture"),
               registry);
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "mass");
  }
}

}  // namespace
