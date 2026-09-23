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

typedef struct { /* sim -> flight software: what the aircraft is doing */
  uint32_t magic, version;
  uint64_t frame;         /* sim major-frame counter */
  double t;               /* sim time, s */
  double phi, theta, psi; /* rad   (v1: truth; later: sensor-model outputs) */
  double p, q, r;         /* rad/s */
  double u, v, w;         /* m/s body */
  double alpha, beta;     /* rad */
  double tas, cas, mach;
  double altitude, vertical_speed; /* m, m/s */
  double lat, lon;                 /* rad */
  double ax, ay, az;               /* m/s^2 body */
  uint32_t status_flags, reserved;
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
