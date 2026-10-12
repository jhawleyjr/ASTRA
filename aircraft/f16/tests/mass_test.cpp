// The F-16 mass plugin against its source, NASA's F16_inertia.dml (M7 PR 6c; R1;
// docs/plans/m7-pr6c-f16-mass-propulsion.md, with the design in docs/plans/m7-pr6b-f16-aircraft.md,
// "Mass"). The expected values come from the extractor's fixture (f16_checkdata.hpp), never from
// the plugin's own literals, and again from SI literals computed offline in exact rational
// arithmetic.
//
// Vol II Table 7's 637.26 slug is reported beside the file's 637.1595 slug, not asserted (D1):
// `ctest --preset asan -R F16Mass -V` shows the report line.
#include <gtest/gtest.h>

#include <Eigen/Core>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <string_view>
#include <utility>

#include "astra/config/config.hpp"
#include "astra/interfaces/mass_properties_model.hpp"
#include "astra/plugin/loader.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/units.hpp"
#include "f16_checkdata.hpp"

namespace {

namespace checkdata = astra::aircraft_f16_checkdata;
namespace units = astra::units;

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

double slug_ft2(double value) { return units::slug_square_feet_to_kilogram_square_meters(value); }

double dml_constant(std::string_view var_id) {
  for (const checkdata::Constant& constant : checkdata::kInertiaConstants) {
    if (constant.var_id == var_id) {
      return constant.value;
    }
  }
  ADD_FAILURE() << "F16_inertia.dml has no constant " << var_id;
  return std::nan("");
}

// The loaded plugin, initialized as Vehicle would initialize it.
class MassUnderTest {
 public:
  MassUnderTest()
      : loaded_(loader_.load(ASTRA_THIS_AIRCRAFT "/physical/mass")),
        mass_(dynamic_cast<astra::MassPropertiesModel*>(loaded_.model.get())) {
    mass_->init(astra::Config::from_string("{}", "f16-mass-test"), registry_);
  }

  [[nodiscard]] const astra::LoadedPlugin& loaded() const { return loaded_; }
  [[nodiscard]] const astra::SignalRegistry& registry() const { return registry_; }
  [[nodiscard]] astra::MassProps props() const { return mass_->get(); }

 private:
  astra::PluginLoader loader_{ASTRA_TEST_PLUGIN_DIR};
  astra::LoadedPlugin loaded_;
  astra::MassPropertiesModel* mass_;
  astra::SignalRegistry registry_;
};

TEST(F16Mass, DescriptorAndSignal) {
  MassUnderTest plugin;
  EXPECT_STREQ(plugin.loaded().descriptor->category, "physical");
  EXPECT_STREQ(plugin.loaded().descriptor->name, "f16_mass");
  EXPECT_EQ(plugin.loaded().model->name(), "mass");
  // The template's one signal and nothing else, in kg, reading the plugin's mass.
  ASSERT_EQ(plugin.registry().signals().size(), 1U);
  const astra::Signal& signal = plugin.registry().signals()[0];
  EXPECT_EQ(signal.name, "mass.mass");
  EXPECT_EQ(signal.unit, "kg");
  EXPECT_EQ(bits(*signal.value), bits(plugin.props().mass));
}

// Each SI value is the file's constant through the same named function, bit for bit. Each is also
// the correctly rounded SI value, computed offline in exact rational arithmetic from Vol II Table
// 73's 4.4482216152605 N per lbf and 0.3048 m per ft, and written to 17 significant digits.
TEST(F16Mass, MatchesTheInertiaDml) {
  MassUnderTest plugin;
  const astra::MassProps props = plugin.props();

  EXPECT_EQ(bits(props.mass), bits(units::slugs_to_kilograms(dml_constant("XMASS"))));
  EXPECT_EQ(bits(props.inertia_b(0, 0)), bits(slug_ft2(dml_constant("XIXX"))));
  EXPECT_EQ(bits(props.inertia_b(1, 1)), bits(slug_ft2(dml_constant("XIYY"))));
  EXPECT_EQ(bits(props.inertia_b(2, 2)), bits(slug_ft2(dml_constant("XIZZ"))));
  // The products enter the tensor negated, as 0.0 - p (the plugin's header comment).
  EXPECT_EQ(bits(props.inertia_b(0, 2)), bits(0.0 - slug_ft2(dml_constant("XIZX"))));
  EXPECT_EQ(bits(props.inertia_b(0, 1)), bits(0.0 - slug_ft2(dml_constant("XIXY"))));
  EXPECT_EQ(bits(props.inertia_b(1, 2)), bits(0.0 - slug_ft2(dml_constant("XIYZ"))));

  struct Literal {
    std::string_view name;
    double actual;
    double expected;
  };
  const std::array<Literal, 5> literals{{
      {"mass (kg)", props.mass, 9298.6438985189388},
      {"Ixx (kg m^2)", props.inertia_b(0, 0), 12874.847237354978},
      {"Iyy (kg m^2)", props.inertia_b(1, 1), 75673.622968168782},
      {"Izz (kg m^2)", props.inertia_b(2, 2), 85552.112539711365},
      {"tensor xz (kg m^2)", props.inertia_b(0, 2), -1331.4132252614352},
  }};
  for (const Literal& literal : literals) {
    EXPECT_LE(std::abs(literal.actual - literal.expected), 1e-12 * std::abs(literal.expected))
        << literal.name << ": " << literal.actual << " against " << literal.expected;
  }
}

// Joe's addition (M7 PR 6c, Deviation 2). tests/golden/nesc/generate_nesc_band.py converts a slug
// value as value * SLUG_TO_KG, with SLUG_TO_KG = 1.0 / KG_TO_SLUG and KG_TO_SLUG = 0.3048 /
// 4.4482216152605 (M7 PR 3c, D2). The two routes agree bit for bit because kKilogramsPerSlug is the
// correctly rounded 4.4482216152605 / 0.3048, which is the same double as SLUG_TO_KG; the
// static_assert below writes the generator's expression out.
TEST(F16Mass, MassIsTheBandGeneratorsSlugRoute) {
  static_assert(units::kKilogramsPerSlug == 1.0 / (0.3048 / 4.4482216152605));
  constexpr double kGeneratorRoute = 9298.643898518938;  // 637.1595 * SLUG_TO_KG, in Python
  EXPECT_EQ(bits(units::slugs_to_kilograms(637.1595)), bits(kGeneratorRoute));
  MassUnderTest plugin;
  EXPECT_EQ(bits(plugin.props().mass), bits(kGeneratorRoute));
}

TEST(F16Mass, ProductsOfInertiaSignAndSymmetry) {
  MassUnderTest plugin;
  const Eigen::Matrix3d inertia = plugin.props().inertia_b;

  // XIZX = 982 slug ft^2 (F16_inertia.dml:87), a positive product of inertia, so the tensor's xz
  // entry is negative.
  EXPECT_LT(inertia(0, 2), 0.0);
  EXPECT_EQ(bits(inertia(0, 2)), bits(0.0 - slug_ft2(982.0)));

  const Eigen::Matrix3d transpose = inertia.transpose();
  for (Eigen::Index row = 0; row < 3; ++row) {
    for (Eigen::Index column = 0; column < 3; ++column) {
      EXPECT_EQ(bits(inertia(row, column)), bits(transpose(row, column)))
          << "(" << row << ", " << column << ")";
    }
  }

  // Ixy = Iyz = 0 in the file: +0.0 in the tensor, never -0.0.
  for (const auto& [row, column] :
       {std::pair{0, 1}, std::pair{1, 0}, std::pair{1, 2}, std::pair{2, 1}}) {
    EXPECT_EQ(inertia(row, column), 0.0) << "(" << row << ", " << column << ")";
    EXPECT_FALSE(std::signbit(inertia(row, column))) << "(" << row << ", " << column << ")";
  }
}

// D1 (docs/plans/m7-f16-inventory.md section 10): the file's mass is used, and Vol II Table 7's is
// reported beside it, never asserted.
TEST(F16Mass, ReportsTableSevenBesideTheDml) {
  constexpr double kDmlSlug = 637.1595;         // F16_inertia.dml:111
  constexpr double kTableSevenSlug = 637.26;    // NASA/TM-2015-218675 Vol II, Table 7, p.15
  constexpr double kStandardGravity = 9.80665;  // m/s^2, for the difference's weight only
  const double dml_kg = units::slugs_to_kilograms(kDmlSlug);
  const double table_seven_kg = units::slugs_to_kilograms(kTableSevenSlug);
  const double difference_kg = table_seven_kg - dml_kg;
  std::cout << std::format(
      "F16Mass.ReportsTableSevenBesideTheDml: F16_inertia.dml {} slug = {} kg; Vol II Table 7 {} "
      "slug = {} kg; Table 7 is {} kg heavier, {} N at {} m/s^2\n",
      kDmlSlug, dml_kg, kTableSevenSlug, table_seven_kg, difference_kg,
      difference_kg * kStandardGravity, kStandardGravity);
}

}  // namespace
