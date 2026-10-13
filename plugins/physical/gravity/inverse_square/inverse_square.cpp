// Inverse-square (Newtonian point-mass) gravity (docs/architecture.md §7, PLAN.md M2 PR 1):
// g_n = [0, 0, g0 * (R/(R+h))^2] plus, when `rotating` is configured true, the centripetal term
// plumb-bob gravity requires (docs/plans/nesc-case05-gravity-centripetal.md): the gravity_model.hpp
// contract is g_n = "gravitation with centripetal folded in", per Stevens/Lewis/Johnson, Aircraft
// Control and Simulation, 3rd ed., eq. 1.6-27, p.33 (g = G - omega x (omega x p)) -- verified by
// direct read, not a supplied citation. For a sphere at latitude lat and radius r = radius_ + h,
// the correction resolved in NED is omega^2 * r * [-sin(lat)*cos(lat), 0, -cos(lat)^2]; this
// matches Groves, Principles of GNSS, Inertial, and Multisensor Integrated Navigation Systems, 2nd
// ed., eq. 2.133 second line (p.70, the NED-resolved form), which reduces to exactly this when
// r_eb^n = (0, 0, -r) -- also verified by direct read, not re-derived from this file's own formula
// alone. gravity/wgs84_j2 already meets this contract (its own citation of Groves 2.133); this
// model did not, at all, until this fix -- the missing term was the leading candidate for NESC
// check-case 5's (round, rotating Earth) disproportionate altitude/v_down miss.
//
// `rotating` (bool, default false) mirrors earth/round's own key exactly and reads the same
// astra::geodesy::kOmegaIe constant when true, rather than taking a numeric rate of its own -- one
// source of truth for the number, no ABI change, and the correction evaluates to exactly zero at
// the default (0.0 times any finite value is 0.0), so every existing non-rotating config/test is
// bit-for-bit unaffected. This plugin does not read earth/round's own `rotating` config and cannot
// detect a mismatch between the two: the two booleans must be set consistently by the config
// author, the same already-accepted duplication as g0/radius below being kept consistent with
// earth/round's own radius (docs/lessons.md, 2026-09-15, "two radii"). An init-time cross-check
// between an EarthModel and a GravityModel's own rotation assumption is deliberately not added
// here (Joe, 2026-09-19, D2(a)); tracked as PLAN.md's own unchecked follow-up item, covering both
// this plugin and gravity/wgs84_j2's own symmetric, currently-latent gap (it rotates
// unconditionally, so pairing it with a non-rotating Earth model is equally unchecked today).
//
// g0 and R both default to the same numbers earth/round defaults to, so the pair reproduce a
// single self-consistent sphere unless a config overrides both; they are not linked automatically
// (a radius concept on EarthModel would mean nothing for flat Earth).
//
// Config keys:
//   g0       | number  | m/s^2 | 9.80665   | Gravity at the sphere's surface: g = g0*(R/(R+h))^2.
//          Finite.
//   radius   | number  | m     | 6371000.0 | The sphere's radius R; finite and positive. Keep it
//          equal to earth/round's radius.
//   rotating | boolean | -     | false     | true adds the Earth rotation's centripetal term
//          (plumb-bob gravity); set it to match earth/round's rotating.
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/gravity_model.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/signals/signal_registry.hpp"
#include "astra/util/geodesy.hpp"

namespace {

using astra::Config;
using astra::ConfigError;

// Same defaults as earth/round: standard gravity at the mean Earth radius.
constexpr double kStandardGravity = 9.80665;
constexpr double kDefaultRadius = 6371000.0;

std::string keyed_path(const Config& config, std::string_view key) {
  return config.path().empty() ? std::string{key} : config.path() + "." + std::string{key};
}

double checked_finite(const Config& config, std::string_view key, double value) {
  if (!std::isfinite(value)) {
    throw ConfigError(config.origin(), keyed_path(config, key),
                      std::string{key} + " must be finite");
  }
  return value;
}

// The SINGULARITY subset of validate_p_n's checks (ADR-0012), shared between in_domain (per
// frame) and validate_p_n (init) so there is exactly one definition of "singular" for this model.
// No polar case: g_n is purely radial, so only altitude matters (latitude sanity is
// EarthModel::validate_p_n's job, checked first). Positive-form comparison (NaN-safe): a
// non-finite altitude reads as kNonFinite rather than silently passing `alt <= -radius`, which is
// false for NaN.
enum class DomainFault : std::uint8_t { kNone, kNonFinite, kBelowSurfaceCenter };

DomainFault domain_fault(double alt, double radius) noexcept {
  if (!std::isfinite(alt)) {
    return DomainFault::kNonFinite;
  }
  if (alt <= -radius) {
    return DomainFault::kBelowSurfaceCenter;
  }
  return DomainFault::kNone;
}

class InverseSquareGravity final : public astra::GravityModel {
 public:
  void init(const Config& config, astra::SignalRegistry& registry) override {
    g0_ = checked_finite(config, "g0", config.number("g0", kStandardGravity));
    radius_ = checked_finite(config, "radius", config.number("radius", kDefaultRadius));
    if (radius_ <= 0.0) {
      throw ConfigError(config.origin(), keyed_path(config, "radius"), "radius must be positive");
    }
    // D1 (docs/plans/nesc-case05-gravity-centripetal.md): a bool mirroring earth/round's own
    // `rotating`, not a numeric rate -- astra::geodesy::kOmegaIe is the single source of truth for
    // the constant, matching earth/round's own unconditional use of it when rotating.
    const bool rotating = config.boolean("rotating", false);
    omega_ = rotating ? astra::geodesy::kOmegaIe : 0.0;
    registry.add("gravity.g0", &g0_, "m/s^2");
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "gravity"; }

  // NOLINTBEGIN(readability-magic-numbers): 0/1/2 are NED north/east/down components.
  [[nodiscard]] Eigen::Vector3d g_n(const astra::VehicleState& vs) const noexcept override {
    const double lat = vs.p_n()(0);
    const double h = vs.p_n()(2);  // altitude above the sphere (ADR-0003)
    const double ratio = radius_ / (radius_ + h);
    // The pure-radial term first, as a literal +0.0 north/east, exactly like the pre-fix return --
    // the centripetal correction below is then ADDED component-wise, not folded into one combined
    // expression. This is required for bit-identity at the default omega_ == 0.0, not just
    // stylistic: -omega_sq_r * sin(lat) * cos(lat) is -0.0 in the northern hemisphere even when
    // omega_sq_r is +0.0 (IEEE-754's sign-of-product rule), and only `+0.0 + (-0.0) == +0.0`
    // (added to the existing literal north component) reproduces the pre-fix bit pattern; a
    // standalone `{north, 0.0, down}` construction using that same -0.0 would not.
    Eigen::Vector3d g{0.0, 0.0, g0_ * ratio * ratio};
    const double r = radius_ + h;
    const double sin_lat = std::sin(lat);
    const double cos_lat = std::cos(lat);
    const double omega_sq_r = omega_ * omega_ * r;
    // Centripetal correction (Stevens/Lewis/Johnson eq. 1.6-27; Groves eq. 2.133, second line, at
    // r_eb^n = (0,0,-r)): exactly zero when omega_ == 0.0 (the non-rotating default), so this is
    // always evaluated unconditionally rather than branched -- see this file's header comment.
    g(0) += -omega_sq_r * sin_lat * cos_lat;
    g(2) += -omega_sq_r * cos_lat * cos_lat;
    return g;
  }
  // NOLINTEND(readability-magic-numbers)

  [[nodiscard]] astra::PositionRepresentation assumed_position_representation()
      const noexcept override {
    return astra::PositionRepresentation::kGeodetic;
  }

  // ADR-0012: per-frame domain validity. g_n() divides by (radius_ + h), singular at alt =
  // -radius_. HealthMonitor checks this post-step, with a one-frame lookahead.
  [[nodiscard]] bool in_domain(const Eigen::Vector3d& p_n) const noexcept override {
    return domain_fault(p_n(2), radius_) ==  // NOLINT(readability-magic-numbers): p_n's altitude
           DomainFault::kNone;
  }

  // Codex adversarial review: earth/round's own validate_p_n bounds altitude against *its*
  // configured radius, which says nothing about this plugin's separately configured radius_ --
  // e.g. earth radius 3000 m, gravity radius 2000 m, altitude -2000 m passes earth/round's floor
  // but makes radius_ + h exactly zero in g_n() below. Bound against this plugin's own radius_.
  // domain_fault is the shared definition behind both this and in_domain above.
  void validate_p_n(const Eigen::Vector3d& p_n, const Config& node) const override {
    const double alt = p_n(2);  // NOLINT(readability-magic-numbers): p_n's third component
    const std::string path = node.path().empty() ? "initial.state.p_n" : node.path() + ".p_n";
    switch (domain_fault(alt, radius_)) {
      case DomainFault::kNonFinite:
        throw ConfigError(node.origin(), path, "p_n's altitude must be finite");
      case DomainFault::kBelowSurfaceCenter:
        throw ConfigError(node.origin(), path,
                          "p_n's altitude is at or below gravity/inverse_square's own configured "
                          "radius' center -- g_n() divides by (radius + altitude)");
      case DomainFault::kNone:
        break;
    }
  }

 private:
  double g0_ = kStandardGravity;
  double radius_ = kDefaultRadius;
  double omega_ = 0.0;  // D1: astra::geodesy::kOmegaIe when `rotating: true`, else exactly 0.0
};

std::unique_ptr<astra::Model> create() { return std::make_unique<InverseSquareGravity>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "gravity_inverse_square",
                                              astra::kCoreApiVersion, astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
