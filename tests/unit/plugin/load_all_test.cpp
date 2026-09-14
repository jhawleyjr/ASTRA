// Loads every plugin the build registered. docs/architecture.md §4 says astra_add_plugin registers
// each plugin "for the test that loads every plugin once and checks its descriptor"; this is it.
#include <gtest/gtest.h>

#include <string>

#include "astra/all_plugins.hpp"
#include "astra/core_api.hpp"
#include "astra/plugin/loader.hpp"

namespace {

TEST(AllPlugins, EveryRegisteredPluginLoadsAndDescribesItself) {
  const astra::PluginLoader loader{ASTRA_TEST_PLUGIN_DIR};
  ASSERT_FALSE(astra::generated::kAllPlugins.empty()) << "the dummy plugin should be registered";

  for (const auto path : astra::generated::kAllPlugins) {
    SCOPED_TRACE(std::string{path});
    const astra::LoadedPlugin loaded = loader.load(path);

    ASSERT_NE(loaded.descriptor, nullptr);
    EXPECT_NE(loaded.descriptor->category, nullptr);
    EXPECT_NE(loaded.descriptor->name, nullptr);
    EXPECT_EQ(loaded.descriptor->core_api_version, astra::core_api_version());
    EXPECT_STREQ(loaded.descriptor->built_with, astra::built_with());
    EXPECT_NE(loaded.descriptor->create, nullptr);

    ASSERT_NE(loaded.model, nullptr);
    EXPECT_FALSE(loaded.model->name().empty());
  }
}

}  // namespace
