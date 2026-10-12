// The WGS-84 ellipsoid, rotating (docs/architecture.md §6, ADR-0003, PLAN.md M2 PR 1): p_n is
// geodetic (latitude, longitude in rad; altitude above the ellipsoid in m). Same
// navigation-equation shape as earth/round (plugins/physical/earth/round), with the meridian radius
// of curvature M(lat) and the prime-vertical radius of curvature N(lat) (core/util/geodesy.hpp) in
// place of round's constant R, and a nonzero omega_ie_n from the Earth's own rotation. Reads no
// config: every constant is a WGS-84 defining parameter, not a per-run choice.
//
// Config keys:
//   none
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

// Same shape as earth/round's own domain_fault (see that file's comment): the SINGULARITY subset
// of validate_p_n's checks (ADR-0012), shared between in_domain (per frame) and validate_p_n
// (init) so there is exactly one definition of "singular" for this model. The surface bound is
// this latitude's *meridian* radius M(lat) rather than a constant R: p_n_dot/omega_en_n divide by
// both (M(lat) + h) and (N(lat) + h), and M(lat) <= N(lat) everywhere (equal only at the poles;
// M/N = (1-e^2)/(1-e^2*sin^2(lat)) <= 1), so M(lat) is the more restrictive bound (Codex review:
// bounding on N(lat) alone would accept an altitude that already makes (M(lat) + h) negative).
enum class DomainFault : std::uint8_t { kNone, kNonFinite, kPolarSingularity, kBelowSurfaceCenter };

DomainFault domain_fault(const Eigen::Vector3d& p_n) noexcept {
  if (!p_n.allFinite()) {
    return DomainFault::kNonFinite;
  }
  const double lat = p_n(0);  // NOLINT(readability-magic-numbers): p_n's lat/lon/alt components
  const double alt = p_n(2);
  if (std::abs(lat) > astra::geodesy::kMaxLatitude) {
    return DomainFault::kPolarSingularity;
  }
  if (alt <= -astra::geodesy::meridian_radius(lat)) {
    return DomainFault::kBelowSurfaceCenter;
  }
  return DomainFault::kNone;
}

class Wgs84Rotating final : public astra::EarthModel {
 public:
  void init(const Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "earth"; }

  // NOLINTBEGIN(readability-magic-numbers): vector component indices below are position/velocity
  // axes (lat/lon/alt, N/E/D), not arbitrary magic numbers.
  [[nodiscard]] Eigen::Vector3d p_n_dot(const astra::VehicleState& vs) const noexcept override {
    const double lat = vs.p_n()(0);
    const double h = vs.p_n()(2);
    const Eigen::Vector3d v_n = astra::frames::rotate_b_to_n(vs.q_nb(), vs.v_b());
    const double m_h = astra::geodesy::meridian_radius(lat) + h;
    const double n_h = astra::geodesy::prime_vertical_radius(lat) + h;
    return {v_n(0) / m_h, v_n(1) / (n_h * std::cos(lat)), -v_n(2)};
  }
  [[nodiscard]] Eigen::Vector3d omega_ie_n(const astra::VehicleState& vs) const noexcept override {
    const double lat = vs.p_n()(0);
    return astra::geodesy::kOmegaIe * Eigen::Vector3d{std::cos(lat), 0.0, -std::sin(lat)};
  }
  [[nodiscard]] Eigen::Vector3d omega_en_n(const astra::VehicleState& vs) const noexcept override {
    const double lat = vs.p_n()(0);
    const double h = vs.p_n()(2);
    const Eigen::Vector3d v_n = astra::frames::rotate_b_to_n(vs.q_nb(), vs.v_b());
    const double m_h = astra::geodesy::meridian_radius(lat) + h;
    const double n_h = astra::geodesy::prime_vertical_radius(lat) + h;
    return {v_n(1) / n_h, -v_n(0) / m_h, -v_n(1) * std::tan(lat) / n_h};
  }
  // NOLINTEND(readability-magic-numbers)
  [[nodiscard]] double altitude(const astra::VehicleState& vs) const noexcept override {
    return vs.p_n()(2);  // p_n's third component is already altitude above the ellipsoid (ADR-0003)
  }

  [[nodiscard]] astra::PositionRepresentation position_representation() const noexcept override {
    return astra::PositionRepresentation::kGeodetic;
  }

  // ADR-0012: per-frame domain validity. See this file's domain_fault comment above for the
  // M(lat)-vs-N(lat) reasoning. HealthMonitor checks this post-step, with a one-frame lookahead, so
  // a trajectory approaching either boundary halts before its kinematics ever go non-finite.
  [[nodiscard]] bool in_domain(const Eigen::Vector3d& p_n) const noexcept override {
    return domain_fault(p_n) == DomainFault::kNone;
  }

  // Same shape as earth/round's validate_p_n (see that file's comment): domain_fault's three
  // singularity checks, interleaved with the two input-sanity checks that stay init-only
  // (degrees-vs-radians, longitude range) in their original priority order, so a single
  // domain_fault() call reproduces every message and every ordering exactly. Init-time only: this
  // validates the initial state; the per-frame check above validates every subsequent one.
  // NOLINTBEGIN(readability-magic-numbers): 0/1 are the p_n vector's lat/lon components.
  void validate_p_n(const Eigen::Vector3d& p_n, const Config& node) const override {
    const std::string path = node.path().empty() ? "initial.state.p_n" : node.path() + ".p_n";
    const DomainFault fault = domain_fault(p_n);
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
                        "p_n's altitude is at or below the ellipsoid's center -- the position "
                        "kinematics divide by (M(lat) + altitude) and (N(lat) + altitude)");
    }
  }
  // NOLINTEND(readability-magic-numbers)
};

std::unique_ptr<astra::Model> create() { return std::make_unique<Wgs84Rotating>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "earth_wgs84_rotating",
                                              astra::kCoreApiVersion, astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
