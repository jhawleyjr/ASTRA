// A spherical, non-rotating Earth (docs/architecture.md §6, ADR-0003, PLAN.md M2 PR 1): p_n is
// geodetic (latitude, longitude in rad; altitude above the sphere in m). omega_ie_n is identically
// zero (non-rotating); omega_en_n is the transport rate of n from motion over the curved surface.
// The kinematics below are the standard flat-non-rotating-sphere navigation equations (Stevens &
// Lewis / Titterton & Weston), with R everywhere in place of round Earth's constant radius --
// wgs84_rotating (plugins/physical/earth/wgs84_rotating) is the same shape with the meridian and
// prime-vertical radii of curvature in place of R and a nonzero omega_ie_n.
#include <cmath>
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

class RoundEarth final : public astra::EarthModel {
 public:
  void init(const Config& config, astra::SignalRegistry& /*registry*/) override {
    radius_ = checked_finite(config, "radius", config.number("radius", kDefaultRadius));
    if (radius_ <= 0.0) {
      throw ConfigError(config.origin(), keyed_path(config, "radius"), "radius must be positive");
    }
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
  [[nodiscard]] Eigen::Vector3d omega_ie_n(
      const astra::VehicleState& /*vs*/) const noexcept override {
    return Eigen::Vector3d::Zero();
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

  // Longitude in [-pi, pi]; latitude bounded away from the poles (astra::geodesy::kMaxLatitude:
  // p_n_dot's cos(lat) divisor and omega_en_n's tan(lat) term are singular at |lat| = pi/2), with a
  // separate message for the obvious failure mode of a degree-valued latitude; altitude finite and
  // above -radius_ (below that, (radius_ + h) itself goes through zero -- a chart singularity, not
  // a terrain check; the atmosphere model's valid_range() is the authority on whether an altitude
  // makes physical sense for a run). This is an init-time check only: it validates the initial
  // state, not every state the integrator visits mid-run (Codex adversarial review). A trajectory
  // that later crosses either boundary is not re-validated, but neither degrades to silent,
  // undetected garbage (verified in tests/unit/earth/earth_kinematics_test.cpp): crossing the
  // altitude floor hits an exact floating-point zero in (radius_ + h), producing literal Inf in
  // p_n_dot/omega_en_n that HealthMonitor's existing per-frame NaN/Inf check (M1 PR 4) catches
  // immediately as a named halt; crossing the polar margin instead produces an enormous but still
  // finite value (cos(lat) near a double's representation of pi/2 is ~1e-17, not exactly zero), not
  // itself a halt, but wildly unphysical enough to cascade into a non-finite or drift-checked
  // quaternion within a frame or two of integration. No new domain-tracking machinery is added for
  // either case: both rely on the same halt-on-NaN backstop the whole per-frame path already uses.
  // NOLINTBEGIN(readability-magic-numbers): 0/1/2 are the p_n vector's lat/lon/alt components.
  void validate_p_n(const Eigen::Vector3d& p_n, const Config& node) const override {
    const double lat = p_n(0);
    const double lon = p_n(1);
    const double alt = p_n(2);
    const std::string path = node.path().empty() ? "initial.state.p_n" : node.path() + ".p_n";
    if (!std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(alt)) {
      throw ConfigError(node.origin(), path, "p_n must be finite");
    }
    if (std::abs(lat) > std::numbers::pi / 2.0) {
      throw ConfigError(node.origin(), path,
                        "p_n's latitude magnitude exceeds pi/2 rad -- latitude is radians, not "
                        "degrees (ADR-0003)");
    }
    if (std::abs(lat) > astra::geodesy::kMaxLatitude) {
      throw ConfigError(node.origin(), path,
                        "p_n's latitude is within the polar singularity margin of +-pi/2 rad; "
                        "the geodetic chart is undefined there");
    }
    if (lon < -std::numbers::pi || lon > std::numbers::pi) {
      throw ConfigError(node.origin(), path, "p_n's longitude must be within [-pi, pi] rad");
    }
    if (alt <= -radius_) {
      throw ConfigError(node.origin(), path,
                        "p_n's altitude is at or below the Earth model's center -- the position "
                        "kinematics divide by (radius + altitude)");
    }
  }
  // NOLINTEND(readability-magic-numbers)

 private:
  double radius_ = kDefaultRadius;
};

std::unique_ptr<astra::Model> create() { return std::make_unique<RoundEarth>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "earth_round", astra::kCoreApiVersion,
                                              astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
