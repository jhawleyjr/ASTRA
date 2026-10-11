// M0 acceptance (docs/architecture.md §16, ADR-0009): the loader loads the dummy plugin by its
// plugin path, rejects a bad core_api_version with a typed error naming the plugin, and the same
// tests pass in the ASTRA_STATIC_PLUGINS build, where nothing is dlopened.
#include "astra/plugin/loader.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/model.hpp"

namespace {

astra::PluginLoader make_loader() { return astra::PluginLoader{ASTRA_TEST_PLUGIN_DIR}; }

bool mentions(const std::exception& error, std::string_view text) {
  return std::string_view{error.what()}.find(text) != std::string_view::npos;
}

TEST(PluginLoader, LoadsTheDummyPluginByPath) {
  const astra::LoadedPlugin loaded = make_loader().load("tests/plugins/dummy");

  ASSERT_NE(loaded.descriptor, nullptr);
  EXPECT_STREQ(loaded.descriptor->category, "test");
  EXPECT_STREQ(loaded.descriptor->name, "dummy");
  EXPECT_EQ(loaded.descriptor->core_api_version, astra::core_api_version());
  EXPECT_STREQ(loaded.descriptor->built_with, astra::built_with());

  ASSERT_NE(loaded.model, nullptr);
  EXPECT_EQ(loaded.model->name(), "dummy");
  loaded.model->reset();
  loaded.model->step(astra::StepContext{.t = 0.0, .dt = 1e-3, .frame = 0});
}

TEST(PluginLoader, EachLoadCreatesItsOwnModel) {
  const astra::PluginLoader loader = make_loader();
  const astra::LoadedPlugin first = loader.load("tests/plugins/dummy");
  const astra::LoadedPlugin second = loader.load("tests/plugins/dummy");
  EXPECT_EQ(first.descriptor, second.descriptor);
  EXPECT_NE(first.model.get(), second.model.get());
}

TEST(PluginLoader, RejectsABadCoreApiVersionWithATypedErrorNamingThePlugin) {
  try {
    const astra::LoadedPlugin loaded = make_loader().load("tests/plugins/bad_version");
    FAIL() << "loaded " << loaded.descriptor->name << " despite its core_api_version";
  } catch (const astra::PluginApiVersionError& error) {
    EXPECT_EQ(error.plugin_path(), "tests/plugins/bad_version");
    EXPECT_TRUE(mentions(error, "tests/plugins/bad_version")) << error.what();
    EXPECT_EQ(error.expected(), astra::core_api_version());
    EXPECT_EQ(error.actual(), astra::core_api_version() + 1);
  }
}

// M7 PR 3b (ADR-0032): every kCoreApiVersion bump must reject a plugin built against the version
// before it -- as of M7 PR 3c (ADR-0033), one built before EnvironmentSample gained altitude_m.
// The plugin's descriptor carries core_api_version() - 1, so this stays "the preceding version"
// through any later bump.
TEST(PluginLoader, RejectsAPluginBuiltAgainstThePrecedingCoreApiVersion) {
  try {
    const astra::LoadedPlugin loaded = make_loader().load("tests/plugins/stale_api_version");
    FAIL() << "loaded " << loaded.descriptor->name << " despite its core_api_version";
  } catch (const astra::PluginApiVersionError& error) {
    EXPECT_EQ(error.plugin_path(), "tests/plugins/stale_api_version");
    EXPECT_EQ(error.expected(), astra::core_api_version());
    EXPECT_EQ(error.actual(), astra::core_api_version() - 1);
    const std::string versions =
        "core_api_version " + std::to_string(astra::core_api_version() - 1) +
        " does not match this core's " + std::to_string(astra::core_api_version());
    EXPECT_TRUE(mentions(error, "tests/plugins/stale_api_version")) << error.what();
    EXPECT_TRUE(mentions(error, versions)) << error.what();
  }
}

TEST(PluginLoader, RejectsABadToolchainStringWithATypedErrorNamingThePlugin) {
  try {
    const astra::LoadedPlugin loaded = make_loader().load("tests/plugins/bad_toolchain");
    FAIL() << "loaded " << loaded.descriptor->name << " despite its toolchain string";
  } catch (const astra::PluginToolchainError& error) {
    EXPECT_EQ(error.plugin_path(), "tests/plugins/bad_toolchain");
    EXPECT_TRUE(mentions(error, "tests/plugins/bad_toolchain")) << error.what();
    EXPECT_EQ(error.expected(), astra::built_with());
    EXPECT_EQ(error.actual(), "stale toolchain");
  }
}

TEST(PluginLoader, ReportsAnUnknownPluginAsNotFound) {
  try {
    const astra::LoadedPlugin loaded =
        make_loader().load("plugins/physical/atmosphere/does_not_exist");
    FAIL() << "loaded " << loaded.descriptor->name << " from nowhere";
  } catch (const astra::PluginNotFoundError& error) {
    EXPECT_EQ(error.plugin_path(), "plugins/physical/atmosphere/does_not_exist");
    EXPECT_TRUE(mentions(error, "plugins/physical/atmosphere/does_not_exist")) << error.what();
  }
}

TEST(PluginLoader, RejectsMalformedPathsBeforeTouchingTheFilesystem) {
  const astra::PluginLoader loader = make_loader();
  for (const std::string_view path :
       {"", "/tests/plugins/dummy", "tests/plugins/dummy/", "tests//plugins/dummy",
        "../tests/plugins/dummy", "tests/plugins/dummy.so", "tests/plugins/dum my"}) {
    SCOPED_TRACE(std::string{path});
    EXPECT_FALSE(astra::PluginLoader::is_valid_plugin_path(path));
    EXPECT_THROW(static_cast<void>(loader.load(path)), astra::PluginNotFoundError);
  }
  EXPECT_TRUE(astra::PluginLoader::is_valid_plugin_path("plugins/physical/atmosphere/isa1976"));
  EXPECT_TRUE(astra::PluginLoader::is_valid_plugin_path("aircraft/_template/physical/aero"));
}

TEST(PluginLoader, MapsAPluginPathToItsLibraryNameAsAdr0009Says) {
  EXPECT_EQ(astra::PluginLoader::library_name("plugins/physical/atmosphere/isa1976"),
            "libastra_plugins_physical_atmosphere_isa1976.so");
  EXPECT_EQ(astra::PluginLoader::library_name("aircraft/notional_transport/physical/aero"),
            "libastra_aircraft_notional_transport_physical_aero.so");
}

TEST(PluginLoader, EveryPluginErrorIsAnAstraError) {
  try {
    static_cast<void>(make_loader().load("tests/plugins/bad_version"));
    FAIL() << "expected an error";
  } catch (const astra::Error& error) {
    EXPECT_TRUE(mentions(error, "tests/plugins/bad_version")) << error.what();
  }
}

TEST(PluginLoader, KnowsWhetherThisBuildIsStatic) {
#if defined(ASTRA_STATIC_PLUGINS)
  EXPECT_TRUE(astra::PluginLoader::is_static());
#else
  EXPECT_FALSE(astra::PluginLoader::is_static());
#endif
  EXPECT_EQ(make_loader().plugin_dir(), ASTRA_TEST_PLUGIN_DIR);
}

}  // namespace
