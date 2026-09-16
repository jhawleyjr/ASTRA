// What p_n means (ADR-0003): every EarthModel says which representation it provides, and a
// GravityModel that reads p_n directly says which one it assumes, so Simulation::from_config can
// reject a mismatch (e.g. gravity/wgs84_j2 paired with earth/flat) at init instead of reading NED
// metres as radians and producing plausible garbage. AtmosphereModel never assumes one of these:
// its at(double altitude_m) (atmosphere_model.hpp) is handed an altitude directly and has no access
// to p_n at all (M2 PR 1b).
#pragma once

#include <cstdint>

namespace astra {

enum class PositionRepresentation : std::uint8_t {
  kAny,       // does not read p_n's geometry (e.g. gravity/constant)
  kFlatNed,   // p_n = NED position from the origin, metres (earth/flat)
  kGeodetic,  // p_n = geodetic latitude, longitude (rad), altitude above the reference surface (m)
};

}  // namespace astra
