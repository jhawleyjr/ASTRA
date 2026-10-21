// A test-only variant of the F-16's aero (M7b PR 4, docs/plans/m7b-pr4-f16-symmetry.md, section 2;
// the M7b plan's Q11, C-04 and C-05).
//
// The shipped plugin, aircraft/f16/physical/aero, keeps F16Aero in its .cpp's anonymous namespace,
// so this plugin carries its own copy of that plugin boundary (aero.cpp:55-80). The copy is pinned
// bitwise to the shipped plugin by F16SymmetrySmoke.SignedTablesEqualTheShippedAero. This plugin
// includes aero_math.hpp by relative path, and never links or loads the shipped plugin. It reads
// one key from its own model block, `test_variant`:
// - signed_tables: build_up(in), the shipped arithmetic, used for the pin;
// - abs_beta_tables: build_up(in, BetaTables::kAbsolute), with the four control-derivative tables
//   read at |beta|. This is the symmetric baseline (Q11);
// - abs_beta_tables_cl0_unsigned: kAbsolute, then clt = absCl0 for every beta (a planted defect,
//   section 10);
// - signed_tables_doubled_odd: kSigned, then each of the four tables gets
//   T(beta) + (T(beta) - T(-beta))/2 (a planted defect, C-05; section 10).
// The defects are written in f16_aero_variant.hpp, beside this file. tests/plugins is skipped by
// scripts/config_keys.py, so the key is not documented there.
#include "f16_aero_variant.hpp"

#include <Eigen/Core>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/aero_model.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/input_registry.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/air_data.hpp"

namespace {

namespace aero = astra::aircraft_f16_aero_math;
namespace defects = astra::test::f16_aero_variant;

enum class Mode : std::uint8_t {
  kSignedTables,
  kAbsBetaTables,
  kAbsBetaTablesCl0Unsigned,
  kSignedTablesDoubledOdd
};

Mode mode_of(const astra::Config& config) {
  const std::string name = config.text("test_variant");
  if (name == "signed_tables") {
    return Mode::kSignedTables;
  }
  if (name == "abs_beta_tables") {
    return Mode::kAbsBetaTables;
  }
  if (name == "abs_beta_tables_cl0_unsigned") {
    return Mode::kAbsBetaTablesCl0Unsigned;
  }
  if (name == "signed_tables_doubled_odd") {
    return Mode::kSignedTablesDoubledOdd;
  }
  const std::string path =
      config.path().empty() ? std::string{"test_variant"} : config.path() + ".test_variant";
  throw astra::ConfigError(config.origin(), path, "unknown test variant: " + name);
}

class F16AeroVariant final : public astra::AeroModel {
 public:
  void init(const astra::Config& config, astra::SignalRegistry& /*registry*/) override {
    mode_ = mode_of(config);
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "aero"; }

  // aero.cpp:49-53, copied.
  void bind_inputs(astra::InputRegistry& inputs, astra::SignalRegistry& registry) override {
    elevator_ = inputs.add(registry, "controls.elevator", "rad");
    aileron_ = inputs.add(registry, "controls.aileron", "rad");
    rudder_ = inputs.add(registry, "controls.rudder", "rad");
  }

  // aero.cpp:55-80, copied, with build_up() replaced by the mode's.
  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& vs, const astra::EnvironmentSample& env) const noexcept override {
    namespace air_data = astra::air_data;

    const Eigen::Vector3d v_rel_b = air_data::relative_velocity_b(vs, env);
    const Eigen::Vector3d omega_eb_b = air_data::omega_eb_b(vs, env);

    aero::Inputs in;
    in.vt = air_data::true_airspeed(v_rel_b);
    in.alpha = air_data::angle_of_attack(v_rel_b);
    in.beta = air_data::sideslip(v_rel_b);
    in.p = omega_eb_b.x();
    in.q = omega_eb_b.y();
    in.r = omega_eb_b.z();
    in.el = *elevator_;
    in.ail = -*aileron_;  // ICD v1 +RWD to the DML's +LWD (D2)
    in.rdr = *rudder_;

    const double density = env.atmosphere.density_kg_m3;
    if (!aero::finite(in) || !std::isfinite(density)) {
      return aero::non_finite_wrench();
    }
    return aero::wrench(build_up(in), air_data::dynamic_pressure(density, in.vt));
  }

 private:
  [[nodiscard]] aero::BuildUp build_up(const aero::Inputs& in) const noexcept {
    switch (mode_) {
      case Mode::kSignedTables:
        return aero::build_up(in);
      case Mode::kAbsBetaTables:
        return aero::build_up(in, aero::BetaTables::kAbsolute);
      case Mode::kAbsBetaTablesCl0Unsigned: {
        aero::BuildUp b = aero::build_up(in, aero::BetaTables::kAbsolute);
        defects::drop_cl0_sign(b);
        return b;
      }
      case Mode::kSignedTablesDoubledOdd: {
        aero::BuildUp b = aero::build_up(in);
        defects::double_odd_parts(in, b);
        return b;
      }
    }
    return aero::build_up(in);
  }

  Mode mode_ = Mode::kSignedTables;
  const double* elevator_ = nullptr;
  const double* aileron_ = nullptr;
  const double* rudder_ = nullptr;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<F16AeroVariant>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "f16_aero_variant", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
