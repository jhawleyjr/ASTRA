/*
 * ASTRA interface control document, version 1 (docs/architecture.md §10, ADR-0006).
 *
 * The frames the simulator and the flight software exchange. Plain C11, <stdint.h> only,
 * fixed-width fields, 8-byte natural alignment, magic and version in every frame, sizes pinned
 * below. A layout is never edited: growing the ICD adds a _v2 struct and bumps ASTRA_ICD_VERSION,
 * so a peer that has not been rebuilt keeps working. Units are SI (ADR-0002); axes and signs follow
 * ADR-0001 (body FRD, nav NED, 3-2-1 Euler angles); frame is the sim's integer major-frame counter
 * (ADR-0010).
 */
#ifndef ASTRA_ICD_H
#define ASTRA_ICD_H

#include <stdint.h>

#ifdef __cplusplus
#define ASTRA_ICD_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#define ASTRA_ICD_ALIGNOF(type) alignof(type)
#else
#define ASTRA_ICD_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#define ASTRA_ICD_ALIGNOF(type) _Alignof(type)
#endif

#define ASTRA_ICD_VERSION 1u
#define ASTRA_ICD_MAGIC 0x41535452u /* "ASTR" */

/*
 * astra_sim_to_sw_v1 status_flags bits (M4 PR 2, docs/plans/m4-lockstep.md Q11/R12a). A set bit
 * means the named field(s) carry a defined placeholder of exactly 0, never NaN; every bit not
 * listed here is 0 in v1.
 */
/* ax/ay/az = 0: frame 0, no previous wrench, a non-finite force, or a non-finite/non-positive mass.
 */
#define ASTRA_ICD_STATUS_ACCEL_INVALID 0x00000001u
/* lat/lon = 0: the Earth model has no geodetic position (flat Earth). */
#define ASTRA_ICD_STATUS_LATLON_NOT_MODELED 0x00000002u
/* mach, cas = 0: no atmosphere model, or non-finite/non-positive pressure or speed of sound. */
#define ASTRA_ICD_STATUS_ATMOSPHERE_INVALID 0x00000004u
/* cas = 0: Mach >= 1, outside the subsonic CAS relation; alpha/beta/tas/mach stay valid. */
#define ASTRA_ICD_STATUS_CAS_SUPERSONIC 0x00000008u
/* tas, alpha, beta, mach, cas = 0: non-finite wind_n, so the air-relative velocity is unknown. */
#define ASTRA_ICD_STATUS_WIND_INVALID 0x00000010u

typedef struct { /* sim -> flight software: what the aircraft is doing */
  uint32_t magic, version;
  uint64_t frame; /* sim major-frame counter */
  double t;       /* sim time, s: exactly (double)frame * dt, dt the fixed step */
  /* 3-2-1 Euler angles of q_nb, rad (v1: truth; later: sensor-model outputs). */
  double phi, theta, psi;
  /* Body angular rate omega_b, rad/s: relative to INERTIAL space (what a gyro measures), not
   * relative to NED. On a rotating Earth the NED-relative rate differs by omega_ie + omega_en. */
  double p, q, r;
  /* Ground-relative body velocity v_b, m/s (truth), not air-relative. */
  double u, v, w;
  /* Air-relative: angle of attack atan2(w_rel, u_rel) and sideslip atan2(v_rel, hypot(u_rel,
   * w_rel)), rad, from v_rel_b = v_b - C_nb wind_n. */
  double alpha, beta;
  /* tas = |v_rel_b|, m/s; mach = tas / speed of sound; cas = calibrated airspeed, m/s, from the
   * subsonic compressible-flow relation with the ISA sea-level reference (p0 = 101325 Pa,
   * a0 = sqrt(1.4 * (8314.32 / 28.9644) * 288.15) m/s), so cas == tas at ISA sea level. */
  double tas, cas, mach;
  /* altitude: the Earth model's altitude above its reference surface, m. vertical_speed: climb
   * rate, m/s, positive up, = -(C_bn v_b)_z, the negated down component of the ground-relative
   * velocity in NED. It equals the rate of change of altitude for every Earth model. */
  double altitude, vertical_speed;
  /* Geodetic latitude and longitude, rad. 0 with ASTRA_ICD_STATUS_LATLON_NOT_MODELED on a flat
   * Earth. */
  double lat, lon;
  /* Specific force in body axes, m/s^2, the IMU convention (z down: level flight at rest reads
   * az = -g). Source, pinned: the total body-axes force of the wrench sources (gravity excluded)
   * from the first integrator stage, evaluated at frame k-1's START state with frame k-1's latched
   * commands, divided by the mass, and packed at frame k. It is NOT evaluated at x_k: one frame
   * of lag. Invalid at frame 0 (ASTRA_ICD_STATUS_ACCEL_INVALID). */
  double ax, ay, az;
  uint32_t status_flags, reserved; /* status_flags: ASTRA_ICD_STATUS_*; reserved is 0 */
} astra_sim_to_sw_v1;

typedef struct { /* flight software -> sim: what the aircraft is told to do */
  uint32_t magic, version;
  uint64_t frame; /* echoes the frame it responds to */
  /* Commanded surface angle, rad. Positive elevator = trailing-edge-down (nose-down pitching
   * moment); positive aileron = right-wing-down rolling moment; positive rudder =
   * trailing-edge-left (nose-left yawing moment) -- the same convention
   * aircraft/notional_transport/physical/aero documents (docs/plans/pr4-notional-aero-turbofan.md's
   * sign table). The sim does not clamp these in v1; range enforcement belongs to the actuator
   * model. */
  double elevator, aileron, rudder;
  double throttle[2]; /* 0..1 per engine */
  uint32_t mode_flags, fault_flags;
} astra_sw_to_sim_v1;

ASTRA_ICD_STATIC_ASSERT(sizeof(astra_sim_to_sw_v1) == 200,
                        "ICD layout changed: bump version"); /* 8+8+8 + 21*8 + 8 */
ASTRA_ICD_STATIC_ASSERT(sizeof(astra_sw_to_sim_v1) == 64,
                        "ICD layout changed: bump version"); /* 8+8 + 5*8 + 8 */
ASTRA_ICD_STATIC_ASSERT(ASTRA_ICD_ALIGNOF(astra_sim_to_sw_v1) == 8, "ICD alignment changed");
ASTRA_ICD_STATIC_ASSERT(ASTRA_ICD_ALIGNOF(astra_sw_to_sim_v1) == 8, "ICD alignment changed");

#endif /* ASTRA_ICD_H */
