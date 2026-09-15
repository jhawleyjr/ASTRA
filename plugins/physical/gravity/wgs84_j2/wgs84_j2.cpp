// WGS-84 gravity from the J2-truncated (oblate) potential plus the centripetal term, plumb-bob by
// construction (docs/architecture.md §7, PLAN.md M2 PR 1). Formula: Groves, "Principles of GNSS,
// Inertial, and Multisensor Integrated Navigation Systems," 2nd ed., eq 2.142 (pure J2 gravitation)
// and eq 2.133 (centripetal, added to the horizontal ECEF components only), computed in ECEF from
// p_n's geodetic (lat, lon, alt) and rotated into NED. Cross-checked in
// tests/unit/gravity/wgs84_j2_test.cpp against the independent Somigliana normal-gravity closed
// form, which agrees to ~1e-4 m/s^2 at the surface (the residual is the physical difference between
// a J2-only truncation and the full normal-gravity ellipsoid, not implementation error -- verified
// numerically before writing this file). Reads no config: every constant is a WGS-84 defining
// parameter.
#include <cmath>
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
#include "astra/util/geodesy.hpp"

namespace {

class Wgs84J2Gravity final : public astra::GravityModel {
 public:
  void init(const astra::Config& /*config*/, astra::SignalRegistry& /*registry*/) override {}
  void reset() override {}
  void step(const astra::StepContext& /*ctx*/) override {}
  [[nodiscard]] std::string_view name() const override { return "gravity"; }

  // NOLINTBEGIN(readability-magic-numbers): 0/1/2 are ECEF x/y/z (and p_n lat/lon/alt) components;
  // 1.5, 3.0, 5.0 are Groves' eq 2.142 coefficients, not arbitrary.
  [[nodiscard]] Eigen::Vector3d g_n(const astra::VehicleState& vs) const noexcept override {
    const double lat = vs.p_n()(0);
    const double lon = vs.p_n()(1);
    const double alt = vs.p_n()(2);
    const Eigen::Vector3d r = astra::geodesy::geodetic_to_ecef(lat, lon, alt);
    const double mag_r = r.norm();
    const double z_scale = 5.0 * (r(2) / mag_r) * (r(2) / mag_r);
    const double j2_factor = 1.5 * astra::geodesy::kJ2 * (astra::geodesy::kSemiMajorAxis / mag_r) *
                             (astra::geodesy::kSemiMajorAxis / mag_r);
    const Eigen::Vector3d correction{(1.0 - z_scale) * r(0), (1.0 - z_scale) * r(1),
                                     (3.0 - z_scale) * r(2)};
    const Eigen::Vector3d gamma_ecef =
        -astra::geodesy::kGm / (mag_r * mag_r * mag_r) * (r + j2_factor * correction);

    Eigen::Vector3d g_ecef = gamma_ecef;
    const double omega_sq = astra::geodesy::kOmegaIe * astra::geodesy::kOmegaIe;
    g_ecef(0) += omega_sq * r(0);
    g_ecef(1) += omega_sq * r(1);  // g_ecef(2) unchanged: centripetal has no ECEF-z component

    return astra::geodesy::ecef_to_ned_dcm(lat, lon) * g_ecef;
  }
  // NOLINTEND(readability-magic-numbers)

  [[nodiscard]] astra::PositionRepresentation assumed_position_representation()
      const noexcept override {
    return astra::PositionRepresentation::kGeodetic;
  }

  // Codex adversarial review: earth/wgs84_rotating's own validate_p_n bounds altitude against the
  // meridian radius M(lat), which keeps its *own* p_n_dot/omega_en_n well-defined, but says nothing
  // about g_n's ECEF radius mag_r above. Near the polar margin, a geodetic (lat, alt) pair can pass
  // that M(lat) floor while still making the *ECEF* radius from the center collapse toward zero:
  // verified numerically (this session) that lat = kMaxLatitude, alt = -N(lat)*(1-e^2) gives
  // mag_r ~ 0.043 m -- the ECEF z-component vanishes exactly there while x and y are already tiny
  // from cos(lat), a genuinely different singularity from the M(lat)/N(lat) floor, since it is the
  // *combination* of near-polar latitude and a specific altitude that collapses all three ECEF
  // components at once. Bounded well below any physically sensible altitude's mag_r (which stays
  // on the order of thousands of kilometres near the real Earth) so this only excludes genuinely
  // degenerate configurations.
  void validate_p_n(const Eigen::Vector3d& p_n, const astra::Config& node) const override {
    const double lat = p_n(0);  // NOLINT(readability-magic-numbers): p_n's lat/lon/alt components
    const double lon = p_n(1);
    const double alt = p_n(2);
    const double mag_r = astra::geodesy::geodetic_to_ecef(lat, lon, alt).norm();
    if (mag_r < kMinSafeRadius) {
      const std::string path = node.path().empty() ? "initial.state.p_n" : node.path() + ".p_n";
      throw astra::ConfigError(
          node.origin(), path,
          "p_n's ECEF radius from the Earth's center is too small for gravity/wgs84_j2's geometry "
          "-- this combination of latitude and altitude is too close to the ellipsoid's own "
          "singularity even though it passes the Earth model's own altitude floor");
    }
  }

 private:
  static constexpr double kMinSafeRadius = 1000.0;  // m; see validate_p_n's comment
};

std::unique_ptr<astra::Model> create() { return std::make_unique<Wgs84J2Gravity>(); }

constexpr astra::PluginDescriptor kDescriptor{"physical", "gravity_wgs84_j2",
                                              astra::kCoreApiVersion, astra::kBuiltWith, &create};

}  // namespace

ASTRA_PLUGIN_EXPORT const astra::PluginDescriptor* astra_plugin() { return &kDescriptor; }
