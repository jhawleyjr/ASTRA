// NASA/TM-2015-218675's sphere drag and brick rate damping (Vol II §B.1.1/B.1.3, PLAN.md M2 PR
// 3): one config-driven WrenchSource covering both vehicles across all ten check cases, since every
// case shares the same two force/moment forms with different coefficients (most zeroed for a given
// case -- e.g. case 1's dragless sphere never even loads this plugin; case 2's brick sets cd, clp,
// cmq, cnr all to zero explicitly to show "no reaction with the atmosphere" as a real, checkable
// zero rather than an absent WrenchSource).
//
// Drag (Vol II §B.1.1, Table 2): F_b = -C_D * 0.5 * rho * S * |v_rel_b| * v_rel_b, the vector form
// of -C_D * q_bar * S * v_hat that has no 0/0 at v_rel_b = 0 (q_bar = 0.5*rho*V^2 and v_hat = v/V
// both blow up there; this form's own |v| factor cancels the same way -C_D*0.5*rho*S*V*v does).
// v_rel_b is air-relative velocity in body axes (wind_model.hpp's own formula):
//     v_rel_b = vs.v_b() - rotate_n_to_b(vs.q_nb(), env.wind_n)
// Checked this session against NASA/TM-2015-218675 Vol II check-case 7, sim_03, t = 0 s (rho =
// 8.906861e-4 slug/ft^3, V = 20 ft/s, S = 0.1963495 ft^2, C_D = 0.1): F_y = 3.4977e-3 lbf, matching
// the published 3.4977165569211e-3 (sim_01's own t=0 density disagrees by 6.9e-4 relative --
// participants do not agree on this to more precision than that).
//
// Rate damping (Vol II §B.1.3, Table 5): L = 0.25*rho*V*S*b^2*C_lp*p, M =
// 0.25*rho*V*S*cbar^2*C_mq*q, N = 0.25*rho*V*S*b^2*C_nr*r, with p, q, r the INERTIAL-referenced
// body rates VehicleState::omega_b already stores (ADR-0003) -- not Earth-relative ones, the same
// distinction check-case 1's own golden (tests/golden/nesc/case1_dragless_sphere.hpp) documents in
// the opposite direction. V here is |v_rel_b|, the same air-relative speed the drag term above
// uses; the form is linear in V, so V = 0 needs no special case either. Checked against check-case
// 3, sim_04, t = 10 s and t = 30 s: ratios 1.000030 / 1.000000 / 1.000030 against L/M/N, the
// residual being that participant's use of exact fractions (2/9 ft^2, 1/3 ft, 2/3 ft) where Vol II
// Table 5 prints the rounded 0.22222 / 0.33333 / 0.66667.
//
// Both cross-checks above are prose, not code: tests/unit/wrench/nesc_aero_test.cpp pins the
// formulas against clean synthetic values instead of these unit-converted NESC numbers, since an
// unguarded unit test asserting a golden-derived number would be an oracle with no guard on it
// (docs/lessons.md's own posture on hand-copied reference values). These two numbers become
// actually executable once PLAN.md's M2 PR 3b lands tests/golden/nesc/ under the guard.
#include <cmath>
#include <memory>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/interfaces/wrench_source.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/frames.hpp"

namespace {

using astra::Config;

class NescAero final : public astra::WrenchSource {
 public:
  void init(const Config& config, astra::SignalRegistry& registry) override {
    s_ = config.number("s", 0.0);
    cd_ = config.number("cd", 0.0);
    b_ = config.number("b", 0.0);
    cbar_ = config.number("cbar", 0.0);
    clp_ = config.number("clp", 0.0);
    cmq_ = config.number("cmq", 0.0);
    cnr_ = config.number("cnr", 0.0);
    // Config-driven, not declared unconditionally (PLAN.md M2 PR 3's blocker fix): only check
    // cases 7 and 8 configure physical.wind. A source that always required wind would reject the
    // eight cases that correctly have none -- see requires_environment() below.
    requires_wind_ = config.boolean("requires_wind", false);
    registry.add("nesc_aero.force_b_x", &force_b_(0), "N");
    registry.add("nesc_aero.force_b_y", &force_b_(1), "N");
    registry.add("nesc_aero.force_b_z", &force_b_(2), "N");
    registry.add("nesc_aero.moment_b_l", &moment_b_(0), "N.m");
    registry.add("nesc_aero.moment_b_m", &moment_b_(1), "N.m");
    registry.add("nesc_aero.moment_b_n", &moment_b_(2), "N.m");
  }
  void reset() override {
    force_b_.setZero();
    moment_b_.setZero();
  }
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "nesc_aero"; }

  [[nodiscard]] astra::Wrench wrench_b(
      const astra::VehicleState& vs, const astra::EnvironmentSample& env) const noexcept override {
    // wind_model.hpp's own formula: air-relative velocity in body axes. env.wind_n is zero by
    // default (no physical.wind configured), so this reduces to vs.v_b() exactly for the eight
    // still-air cases.
    const Eigen::Vector3d v_rel_b = vs.v_b() - astra::frames::rotate_n_to_b(vs.q_nb(), env.wind_n);
    const double speed = v_rel_b.norm();
    const double rho = env.atmosphere.density_kg_m3;

    astra::Wrench wrench;
    // -C_D * 0.5 * rho * S * |v| * v: no 0/0 at speed == 0 (this file's header). 0.5 and 0.25
    // below are the drag/damping forms' own coefficients (Vol II §B.1.1/§B.1.3), and the (0)/(1)/
    // (2) indices are body roll/pitch/yaw components -- none are arbitrary magic numbers.
    // NOLINTBEGIN(readability-magic-numbers)
    wrench.force_b = -cd_ * 0.5 * rho * s_ * speed * v_rel_b;

    const double p = vs.omega_b()(0);
    const double q = vs.omega_b()(1);
    const double r = vs.omega_b()(2);
    const double damping_common = 0.25 * rho * speed * s_;
    wrench.moment_b(0) = damping_common * b_ * b_ * clp_ * p;
    wrench.moment_b(1) = damping_common * cbar_ * cbar_ * cmq_ * q;
    wrench.moment_b(2) = damping_common * b_ * b_ * cnr_ * r;
    // NOLINTEND(readability-magic-numbers)

    // mutable, for the registry publish above -- test instrumentation only, the same exception
    // tests/plugins/wind_consumer.cpp documents for its own const wrench_b.
    force_b_ = wrench.force_b;
    moment_b_ = wrench.moment_b;
    return wrench;
  }

  [[nodiscard]] astra::EnvironmentRequirements requires_environment() const noexcept override {
    astra::EnvironmentRequirements requirements;
    requirements.wind = requires_wind_;
    requirements.atmosphere = true;  // every NESC check case configures physical.atmosphere
    return requirements;
  }

 private:
  double s_ = 0.0;
  double cd_ = 0.0;
  double b_ = 0.0;
  double cbar_ = 0.0;
  double clp_ = 0.0;
  double cmq_ = 0.0;
  double cnr_ = 0.0;
  bool requires_wind_ = false;
  mutable Eigen::Vector3d force_b_ = Eigen::Vector3d::Zero();
  mutable Eigen::Vector3d moment_b_ = Eigen::Vector3d::Zero();
};

std::unique_ptr<astra::Model> create() { return std::make_unique<NescAero>(); }

constexpr astra::PluginDescriptor kDescriptor{"test", "nesc_aero", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
