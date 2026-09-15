// What p_n means (ADR-0003): every EarthModel says which representation it provides, and a
// GravityModel or AtmosphereModel that reads p_n directly says which one it assumes, so
// Simulation::from_config can reject a mismatch (e.g. gravity/wgs84_j2 paired with earth/flat) at
// init instead of reading NED metres as radians and producing plausible garbage.
#pragma once

#include <cstdint>

namespace astra {

enum class PositionRepresentation : std::uint8_t {
  kAny,       // does not read p_n's geometry (e.g. gravity/constant, atmosphere/isa1976)
  kFlatNed,   // p_n = NED position from the origin, metres (earth/flat)
  kGeodetic,  // p_n = geodetic latitude, longitude (rad), altitude above the reference surface (m)
};

}  // namespace astra
