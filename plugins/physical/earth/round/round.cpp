// A spherical Earth (docs/architecture.md §6, ADR-0003, PLAN.md M2 PR 1): p_n is geodetic
// (latitude, longitude in rad; altitude above the sphere in m). omega_en_n is the transport rate of
// n from motion over the curved surface, always. omega_ie_n is zero by default (non-rotating,
// PLAN.md M2 PR 1's own scope) or the Earth's own rotation when `rotating: true` (M2 PR 3, NESC
// check-case 5: Table 6.3-1 pairs case 4 fixed with case 5 rotating, same 1/R^2 gravity, to isolate
// the effect of Earth rotation alone) -- the same omega_ie_n(lat) formula wgs84_rotating uses,
// since that formula depends only on latitude, not on which radius of curvature the rest of this
// model's kinematics use. The kinematics below are the standard flat-non-rotating-sphere navigation
// equations (Stevens & Lewis / Titterton & Weston), with R everywhere in place of round Earth's
// constant radius -- wgs84_rotating (plugins/physical/earth/wgs84_rotating) is the same shape with
// the meridian and prime-vertical radii of curvature in place of R, and rotating unconditionally.
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>

#include "astra/config/config.hpp"
#include "astra/core_api.hpp"
#include "astra/errors.hpp"
#include "astra/interfaces/earth_model.hpp"
#include "astra/interfaces/position_representation.hpp"
#include "astra/kernel/vehicle_state.hpp"
#include "astra/plugin/descriptor.hpp"
#include "astra/util/frames.hpp"
#include "astra/util/geodesy.hpp"

namespace {

using astra::Config;
using astra::ConfigError;

// Mean Earth radius, m (IUGG). Not a WGS-84 constant -- geodesy.hpp's kSemiMajorAxis is the
// ellipsoid's semi-major axis, which is a different number for a genuinely spherical model.
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

// The three faults p_n_dot/omega_en_n's own math can hit -- the SINGULARITY subset of
// validate_p_n's checks (ADR-0012), shared between in_domain (per frame) and validate_p_n (init),
// so there is exactly one definition of "singular" for this model. Deliberately excludes
// validate_p_n's own input-sanity checks (degree-valued latitude, longitude range): those are
// properties of a config value, not of where the kinematics are defined, and must stay init-only
// -- re-running them per frame would halt any antimeridian crossing (ADR-0012's Context).
enum class DomainFault : std::uint8_t { kNone, kNonFinite, kPolarSingularity, kBelowSurfaceCenter };

// Positive-form comparisons throughout (NaN-safe): a non-finite p_n reads as kNonFinite rather
// than silently passing a `>`/`<=` comparison that is false for NaN. Priority order matches
// validate_p_n's own domain-check order (finite, then polar, then altitude floor) with its
// input-sanity checks omitted -- see validate_p_n below for how the two interleave.
DomainFault domain_fault(const Eigen::Vector3d& p_n, double radius) noexcept {
  if (!p_n.allFinite()) {
    return DomainFault::kNonFinite;
  }
  const double lat = p_n(0);  // NOLINT(readability-magic-numbers): p_n's lat/lon/alt components
  const double alt = p_n(2);
  if (std::abs(lat) > astra::geodesy::kMaxLatitude) {
    return DomainFault::kPolarSingularity;
  }
  if (alt <= -radius) {
    return DomainFault::kBelowSurfaceCenter;
  }
  return DomainFault::kNone;
}

class RoundEarth final : public astra::EarthModel {
 public:
  void init(const Config& config, astra::SignalRegistry& /*registry*/) override {
    radius_ = checked_finite(config, "radius", config.number("radius", kDefaultRadius));
    if (radius_ <= 0.0) {
      throw ConfigError(config.origin(), keyed_path(config, "radius"), "radius must be positive");
    }
    rotating_ = config.boolean("rotating", false);
  }
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "earth"; }

  // NOLINTBEGIN(readability-magic-numbers): vector component indices below are position/velocity
  // axes (lat/lon/alt, N/E/D), not arbitrary magic numbers.
  [[nodiscard]] Eigen::Vector3d p_n_dot(const astra::VehicleState& vs) const noexcept override {
    const double lat = vs.p_n()(0);
    const double h = vs.p_n()(2);
    const Eigen::Vector3d v_n = astra::frames::rotate_b_to_n(vs.q_nb(), vs.v_b());
    const double r_h = radius_ + h;
    return {v_n(0) / r_h, v_n(1) / (r_h * std::cos(lat)), -v_n(2)};
  }
  [[nodiscard]] Eigen::Vector3d omega_ie_n(const astra::VehicleState& vs) const noexcept override {
    if (!rotating_) {
      return Eigen::Vector3d::Zero();
    }
    // Same formula as wgs84_rotating::omega_ie_n -- depends only on latitude, not on which radius
    // of curvature this model's own p_n_dot/omega_en_n use.
    const double lat = vs.p_n()(0);  // NOLINT(readability-magic-numbers): p_n's latitude component
    return astra::geodesy::kOmegaIe * Eigen::Vector3d{std::cos(lat), 0.0, -std::sin(lat)};
  }
  [[nodiscard]] Eigen::Vector3d omega_en_n(const astra::VehicleState& vs) const noexcept override {
    const double lat = vs.p_n()(0);
    const double h = vs.p_n()(2);
    const Eigen::Vector3d v_n = astra::frames::rotate_b_to_n(vs.q_nb(), vs.v_b());
    const double r_h = radius_ + h;
    return {v_n(1) / r_h, -v_n(0) / r_h, -v_n(1) * std::tan(lat) / r_h};
  }
  // NOLINTEND(readability-magic-numbers)
  [[nodiscard]] double altitude(const astra::VehicleState& vs) const noexcept override {
    return vs.p_n()(2);  // p_n's third component is already altitude above the sphere (ADR-0003)
  }

  [[nodiscard]] astra::PositionRepresentation position_representation() const noexcept override {
    return astra::PositionRepresentation::kGeodetic;
  }

  // ADR-0012: per-frame domain validity. p_n_dot's cos(lat) divisor and omega_en_n's tan(lat) term
  // are singular at |lat| = pi/2 (bounded here at astra::geodesy::kMaxLatitude); both also divide
  // by (radius_ + h), singular at alt = -radius_. HealthMonitor checks this post-step, with a
  // one-frame lookahead, so a trajectory approaching either boundary halts before its kinematics
  // ever go non-finite (closes the gap PR 1 disclosed and this file used to document at length).
  [[nodiscard]] bool in_domain(const Eigen::Vector3d& p_n) const noexcept override {
    return domain_fault(p_n, radius_) == DomainFault::kNone;
  }

  // Longitude in [-pi, pi]; latitude bounded away from the poles, with a separate message for the
  // obvious failure mode of a degree-valued latitude; altitude finite and above -radius_ (the
  // atmosphere model's valid_range() is the authority on whether an altitude makes physical sense
  // for a run, not this check). ADR-0012: this is domain_fault's three singularity checks above,
  // interleaved with the two input-sanity checks that stay init-only (degrees-vs-radians, longitude
  // range) in their original priority order -- finite, degrees, polar margin, longitude, altitude
  // floor -- so a single domain_fault() call, examined at each domain-check slot, reproduces every
  // message and every ordering exactly. Init-time only: this validates the initial state; the
  // per-frame check above validates every subsequent one.
  // NOLINTBEGIN(readability-magic-numbers): 0/1 are the p_n vector's lat/lon components.
  void validate_p_n(const Eigen::Vector3d& p_n, const Config& node) const override {
    const std::string path = node.path().empty() ? "initial.state.p_n" : node.path() + ".p_n";
    const DomainFault fault = domain_fault(p_n, radius_);
    if (fault == DomainFault::kNonFinite) {
      throw ConfigError(node.origin(), path, "p_n must be finite");
    }
    const double lat = p_n(0);
    if (std::abs(lat) > std::numbers::pi / 2.0) {
      throw ConfigError(node.origin(), path,
                        "p_n's latitude magnitude exceeds pi/2 rad -- latitude is radians, not "
                        "degrees (ADR-0003)");
    }
    if (fault == DomainFault::kPolarSingularity) {
      throw ConfigError(node.origin(), path,
                        "p_n's latitude is within the polar singularity margin of +-pi/2 rad; "
                        "the geodetic chart is undefined there");
    }
    const double lon = p_n(1);
    if (lon < -std::numbers::pi || lon > std::numbers::pi) {
      throw ConfigError(node.origin(), path, "p_n's longitude must be within [-pi, pi] rad");
    }
    if (fault == DomainFault::kBelowSurfaceCenter) {
      throw ConfigError(node.origin(), path,
                        "p_n's altitude is at or below the Earth model's center -- the position "
                        "kinematics divide by (radius + altitude)");
    }
  }
  // NOLINTEND(readability-magic-numbers)

 private:
  double radius_ = kDefaultRadius;
  bool rotating_ = false;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<RoundEarth>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "earth_round", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
