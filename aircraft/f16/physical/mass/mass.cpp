// The F-16 mass properties (M7 PR 6c; docs/plans/m7-pr6c-f16-mass-propulsion.md, with the design in
// docs/plans/m7-pr6b-f16-aircraft.md, "Mass"; R1): NASA's F16_inertia.dml, which Vol II calls
// normative. The mass is the file's 637.1595 slug (D1, docs/plans/m7-f16-inventory.md section 10);
// Vol II Table 7's 637.26 slug is reported beside it by the F16Mass tests, never used here.
//
// Every number is the file's, converted once through a named core/util/units function (ADR-0002):
// slugs_to_kilograms and slug_square_feet_to_kilogram_square_meters. The plugin reads no config
// key: a config block is the plugin path alone.
//
// The file lists products of inertia (XIZX is "bodyProductOfInertia_ZX", Ixz = integral(x*z*dm)),
// the S&L convention M7 PR 4 made uniform. The inertia TENSOR the equations of motion use
// (H = I*omega) carries their negations, so its xz entry is negative. Each negation is written
// 0.0 - p, not -p, as plugins/physical/mass/constant writes it (M7 PR 4, D1): the zero products Ixy
// and Iyz then put +0.0 off the diagonal, where -p would put -0.0. ASTRA builds without
// -ffast-math, so the compiler cannot fold 0.0 - p into -p.
//
// Everything sits in an anonymous namespace, and the descriptor's name is "f16_mass" (the aero's
// reasoning, physical/aero/aero.cpp).
#include <Eigen/Cholesky>
#include <Eigen/Core>
#include <memory>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/mass_properties_model.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/units.hpp"

namespace {

namespace units = astra::units;

// NOLINTBEGIN(readability-magic-numbers): each literal is an initialValue of F16_inertia.dml
// (SHA-256 e11d4eba..., docs/plans/m7-f16-inventory.md section 1.1), cited on its line.
constexpr double kMass = units::slugs_to_kilograms(637.1595);  // XMASS, slug (:111)
constexpr double kIxx = units::slug_square_feet_to_kilogram_square_meters(9496.0);   // XIXX (:63)
constexpr double kIyy = units::slug_square_feet_to_kilogram_square_meters(55814.0);  // XIYY (:71)
constexpr double kIzz = units::slug_square_feet_to_kilogram_square_meters(63100.0);  // XIZZ (:79)
// The products of inertia, in the file's (S&L) convention.
constexpr double kIzx = units::slug_square_feet_to_kilogram_square_meters(982.0);  // XIZX (:87)
constexpr double kIxy = units::slug_square_feet_to_kilogram_square_meters(0.0);    // XIXY (:95)
constexpr double kIyz = units::slug_square_feet_to_kilogram_square_meters(0.0);    // XIYZ (:103)
// NOLINTEND(readability-magic-numbers)

class F16Mass final : public astra::MassPropertiesModel {
 public:
  void init(const astra::Config& config, astra::SignalRegistry& registry) override {
    Eigen::Matrix3d inertia_b;
    // clang-format off
    inertia_b <<       kIxx, 0.0 - kIxy, 0.0 - kIzx,
                 0.0 - kIxy,       kIyy, 0.0 - kIyz,
                 0.0 - kIzx, 0.0 - kIyz,       kIzz;
    // clang-format on

    // The template's check, kept (§15: "inertia tensor symmetric positive-definite"). P1's numbers
    // pass it; it guards an edit of the constants above.
    const Eigen::LLT<Eigen::Matrix3d> llt(inertia_b);
    if (llt.info() != Eigen::Success) {
      throw astra::ConfigError(config.origin(), config.path(),
                               "the F-16 inertia tensor must be positive-definite");
    }

    props_.mass = kMass;
    props_.inertia_b = inertia_b;
    registry.add("mass.mass", &props_.mass, "kg");
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "mass"; }

  [[nodiscard]] astra::MassProps get() const noexcept override { return props_; }

 private:
  astra::MassProps props_;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<F16Mass>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "f16_mass", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
