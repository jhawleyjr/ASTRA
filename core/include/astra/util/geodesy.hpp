// core/util/ owns geodesy the same way frames.hpp owns frame/attitude conversions (§4's "frames,
// quaternions, geodesy"). WGS-84 defining constants and the two radii of curvature every
// round/WGS-84 Earth or gravity model needs (docs/architecture.md §6, ADR-0003), typed from the
// WGS-84 (EGM96) defining parameters (NIMA TR8350.2, 3rd ed.).
#pragma once

#include <Eigen/Core>
#include <cmath>
#include <numbers>

namespace astra::geodesy {

// NOLINTBEGIN(readability-magic-numbers): every literal is a WGS-84 defining or derived constant,
// not an arbitrary magic number; naming each one further would not make it easier to check.

// Semi-major axis, metres.
inline constexpr double kSemiMajorAxis = 6378137.0;
// Flattening, 1/f = 298.257223563.
inline constexpr double kFlattening = 1.0 / 298.257223563;
// First eccentricity squared: e^2 = f * (2 - f).
inline constexpr double kEccentricitySquared = kFlattening * (2.0 - kFlattening);
// Earth's rotation rate, rad/s (WGS-84 value, includes the effect of precession).
inline constexpr double kOmegaIe = 7.292115e-5;
// Earth's gravitational constant (mass of Earth including the atmosphere), m^3/s^2.
inline constexpr double kGm = 3.986004418e14;
// Second degree zonal harmonic (dynamical form factor), unnormalized.
inline constexpr double kJ2 = 1.082629989e-3;

// The largest latitude magnitude the geodetic chart is used at (rad): pi/2 minus a small margin
// (~6 m of arc at the surface). lon_dot = v_E / ((N+h)*cos(lat)) and the tan(lat) term in
// omega_en_n are singular exactly at the pole; EarthModel::validate_p_n (round, wgs84_rotating)
// rejects any p_n whose latitude exceeds this, so the integrator never sees the singularity. A
// judgment call, not a derived number -- stated once here so a later nonsingular chart supersedes
// it cleanly.
inline constexpr double kMaxLatitude = std::numbers::pi / 2.0 - 1e-6;

// Meridian radius of curvature M(lat), metres: the radius of curvature in the plane of the
// meridian, what a due-north/south displacement divides by.
[[nodiscard]] inline double meridian_radius(double latitude) noexcept {
  const double sin_lat = std::sin(latitude);
  const double denominator = 1.0 - kEccentricitySquared * sin_lat * sin_lat;
  return kSemiMajorAxis * (1.0 - kEccentricitySquared) / std::pow(denominator, 1.5);
}

// Prime vertical (transverse) radius of curvature N(lat), metres: the radius of curvature in the
// plane perpendicular to the meridian, what a due-east/west displacement divides by (together with
// cos(lat), since the meridians converge toward the poles).
[[nodiscard]] inline double prime_vertical_radius(double latitude) noexcept {
  const double sin_lat = std::sin(latitude);
  const double denominator = 1.0 - kEccentricitySquared * sin_lat * sin_lat;
  return kSemiMajorAxis / std::sqrt(denominator);
}

// Geodetic (latitude, longitude, altitude above the ellipsoid) to ECEF Cartesian position, m.
// Needed by gravity/wgs84_j2's J2 spherical-harmonic gravity formula (Groves, "Principles of GNSS,
// Inertial, and Multisensor Integrated Navigation Systems," 2nd ed., eq 2.142), which is naturally
// expressed in ECEF; round/wgs84_rotating's navigation-equation kinematics stay in NED throughout
// and never need this. The reverse conversion (ecef_to_geodetic) has no caller in this PR and is
// not added.
[[nodiscard]] inline Eigen::Vector3d geodetic_to_ecef(double latitude, double longitude,
                                                      double altitude) noexcept {
  const double n = prime_vertical_radius(latitude);
  const double cos_lat = std::cos(latitude);
  const double sin_lat = std::sin(latitude);
  return {(n + altitude) * cos_lat * std::cos(longitude),
          (n + altitude) * cos_lat * std::sin(longitude),
          (n * (1.0 - kEccentricitySquared) + altitude) * sin_lat};
}

// Rotation from ECEF to the local NED frame at (latitude, longitude): rows are the North, East,
// Down unit vectors expressed in ECEF, so g_n = ecef_to_ned_dcm(lat, lon) * g_ecef.
[[nodiscard]] inline Eigen::Matrix3d ecef_to_ned_dcm(double latitude, double longitude) noexcept {
  const double cos_lat = std::cos(latitude);
  const double sin_lat = std::sin(latitude);
  const double cos_lon = std::cos(longitude);
  const double sin_lon = std::sin(longitude);
  Eigen::Matrix3d c_ne;
  // clang-format off
  c_ne << -sin_lat * cos_lon, -sin_lat * sin_lon,  cos_lat,
          -sin_lon,            cos_lon,            0.0,
          -cos_lat * cos_lon, -cos_lat * sin_lon, -sin_lat;
  // clang-format on
  return c_ne;
}

// NOLINTEND(readability-magic-numbers)

}  // namespace astra::geodesy
