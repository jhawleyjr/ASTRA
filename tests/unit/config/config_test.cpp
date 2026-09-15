// Config (core/config/config.hpp, docs/architecture.md §13, §15): typed getters and defaults,
// nested maps and sequences, a missing key and a wrong type each naming the full dotted path, a
// malformed document, and unknown_keys() (§15: unknown keys are errors, not warnings).
#include "astra/config/config.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "astra/errors.hpp"

namespace {

using astra::Config;
using astra::ConfigError;

bool mentions(const std::exception& error, std::string_view text) {
  return std::string_view{error.what()}.find(text) != std::string_view::npos;
}

constexpr std::string_view kDocument = R"(
sim:
  dt: 0.001
  frames: 1000
  strict: true
vehicle:
  models:
    - { plugin: tests/plugins/dummy, divisor: 1 }
    - plugins/physical/earth/flat
name: notional
)";

TEST(Config, ReadsScalarsOfEveryType) {
  const Config config = Config::from_string(kDocument, "test");
  EXPECT_DOUBLE_EQ(config.number("sim.dt"), 0.001);
  EXPECT_EQ(config.integer("sim.frames"), 1000);
  EXPECT_TRUE(config.boolean("sim.strict"));
  EXPECT_EQ(config.text("name"), "notional");
}

TEST(Config, DefaultedGettersFallBackOnlyWhenTheKeyIsMissing) {
  const Config config = Config::from_string(kDocument, "test");
  EXPECT_DOUBLE_EQ(config.number("sim.dt", 5.0), 0.001) << "present key must win over the default";
  EXPECT_DOUBLE_EQ(config.number("sim.missing", 5.0), 5.0);
  EXPECT_EQ(config.integer("sim.missing", 7), 7);
  EXPECT_FALSE(config.boolean("sim.missing", false));
  EXPECT_EQ(config.text("sim.missing", "fallback"), "fallback");
}

TEST(Config, NavigatesNestedMapsAndSequences) {
  const Config config = Config::from_string(kDocument, "test");
  const Config models = config.at("vehicle.models");
  ASSERT_TRUE(models.is_sequence());
  ASSERT_EQ(models.size(), 2U);

  const Config first = models.at(0);
  ASSERT_TRUE(first.is_map());
  EXPECT_EQ(first.text("plugin"), "tests/plugins/dummy");
  EXPECT_EQ(first.integer("divisor"), 1);

  const Config second = models.at(1);
  ASSERT_TRUE(second.is_scalar());
  EXPECT_EQ(second.text(), "plugins/physical/earth/flat");
}

TEST(Config, HasReportsPresenceWithoutThrowing) {
  const Config config = Config::from_string(kDocument, "test");
  EXPECT_TRUE(config.has("sim.dt"));
  EXPECT_FALSE(config.has("sim.nonexistent"));
  EXPECT_FALSE(config.has("nonexistent.nested.path"));
}

TEST(Config, MissingKeyNamesTheFullDottedPathAndOrigin) {
  const Config config = Config::from_string(kDocument, "test-origin");
  try {
    static_cast<void>(config.number("sim.does_not_exist"));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "sim.does_not_exist");
    EXPECT_EQ(error.origin(), "test-origin");
    EXPECT_TRUE(mentions(error, "sim.does_not_exist")) << error.what();
    EXPECT_TRUE(mentions(error, "test-origin")) << error.what();
  }
}

TEST(Config, WrongTypeNamesTheFullDottedPath) {
  const Config config = Config::from_string(kDocument, "test");
  try {
    static_cast<void>(config.integer("name"));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "name");
    EXPECT_TRUE(mentions(error, "name")) << error.what();
  }
}

TEST(Config, NestedMissingKeyNamesThePathFromTheRoot) {
  const Config config = Config::from_string(kDocument, "test");
  try {
    static_cast<void>(config.at("vehicle.models").at(0).text("does_not_exist"));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    EXPECT_EQ(error.config_path(), "vehicle.models.0.does_not_exist");
  }
}

TEST(Config, MalformedDocumentThrowsConfigError) {
  EXPECT_THROW(static_cast<void>(Config::from_string("sim: [unterminated", "bad")), ConfigError);
}

TEST(Config, FromFileNamesAMissingFileAsAConfigError) {
  EXPECT_THROW(static_cast<void>(Config::from_file("/nonexistent/path/astra_config_test.yaml")),
               ConfigError);
}

TEST(Config, UnknownKeysReturnsEveryLeafNoGetterHasRead) {
  const Config config = Config::from_string(kDocument, "test");
  EXPECT_EQ(config.unknown_keys().size(), 7U) << "nothing has been read yet";

  static_cast<void>(config.number("sim.dt"));
  static_cast<void>(config.integer("sim.frames"));
  static_cast<void>(config.boolean("sim.strict"));
  static_cast<void>(config.text("name"));
  static_cast<void>(config.at("vehicle.models").at(0).text("plugin"));
  static_cast<void>(config.at("vehicle.models").at(0).integer("divisor"));
  static_cast<void>(config.at("vehicle.models").at(1).text());

  EXPECT_TRUE(config.unknown_keys().empty());
}

TEST(Config, UnknownKeysNamesTheOneKeyNoGetterConsumed) {
  const Config config = Config::from_string("a: 1\nb: 2\n", "test");
  static_cast<void>(config.integer("a"));
  const std::vector<std::string> unknown = config.unknown_keys();
  ASSERT_EQ(unknown.size(), 1U);
  EXPECT_EQ(unknown.front(), "b");
}

}  // namespace
