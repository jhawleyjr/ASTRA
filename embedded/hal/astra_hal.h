/*
 * The reference HAL the example flight software is written against (docs/architecture.md §11;
 * ADR-0008, ADR-0024). C11 and C++17, <stdint.h> and icd/astra_icd.h only. Backend in this build:
 * shm (embedded/hal/shm), on the shared region layout in embedded/hal/astra_shm_layout.h.
 *
 * One session per process (module-static state). Typical use:
 *
 *   astra_hal_config cfg = {segment, handshake_timeout_ms, peer_timeout_ms};  // from the driver
 *   astra_hal_status s = hal_init(&cfg);
 *   while (s == ASTRA_HAL_OK && (s = hal_wait_frame(&in)) == ASTRA_HAL_OK) {
 *     ... compute out from in (zero it first), hal_now() is in.t ...
 *     s = hal_send(&out);
 *   }
 *   hal_close();
 *   return hal_exit_status(s);
 *
 * No function here allocates, and none reads a clock except the one bounded-wait function inside
 * the shm backend (ADR-0023 site 2); hal_now() is frame-derived.
 */
#ifndef ASTRA_HAL_H
#define ASTRA_HAL_H

#include <stdint.h>

#include "icd/astra_icd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ASTRA_HAL_OK = 0,
  /* The sim ended the session normally (sim_state CLOSED or req_seq CLOSE_SEQ). Exit 0. */
  ASTRA_HAL_CLOSED = 1,
  /* No request within peer_timeout_ms: the sim is dead, failed, or idle too long (ADR-0024). */
  ASTRA_HAL_TIMEOUT = 2,
  /* No segment, a segment never sized, or never SIM_READY within handshake_timeout_ms. */
  ASTRA_HAL_HANDSHAKE_TIMEOUT = 3,
  /* Magic, layout version, layout size, or ICD version differ from this build's. */
  ASTRA_HAL_BAD_LAYOUT = 4,
  /* A stale or foreign session, CLOSED during the handshake, or a request that disagrees with
   * req_seq (magic, version, or the 64-bit frame). */
  ASTRA_HAL_PROTOCOL = 5,
  /* A system call failed, a bad argument, or the calls were out of order. */
  ASTRA_HAL_ERROR = 6
} astra_hal_status;

typedef struct {
  const char* segment;           /* astra_<driver pid>_<nonce>, as given to the sim */
  uint32_t handshake_timeout_ms; /* the whole attach, one deadline; > 0 */
  uint32_t peer_timeout_ms;      /* each hal_wait_frame; >= the sim's timeout_ms; > 0 */
} astra_hal_config;

/* Attaches and completes the header-only handshake (ADR-0024). OK, CLOSED is never returned here
 * (CLOSED during the handshake is PROTOCOL), HANDSHAKE_TIMEOUT, BAD_LAYOUT, PROTOCOL, or ERROR. */
astra_hal_status hal_init(const astra_hal_config* config);

/* Blocks for the next request and copies it into *in. OK, CLOSED (takes precedence over expiry),
 * TIMEOUT, PROTOCOL, or ERROR (also when the previous frame's reply has not been sent). */
astra_hal_status hal_wait_frame(astra_sim_to_sw_v1* in);

/* Publishes *out as the reply to the frame hal_wait_frame last returned (all 64 bytes; the ack is
 * that frame's sequence word, never out->frame). OK, or ERROR when no frame is pending. */
astra_hal_status hal_send(const astra_sw_to_sim_v1* out);

/* Frame-derived time: the t of the last frame hal_wait_frame returned, 0.0 before any. */
double hal_now(void);

/* Unmaps the region. Writes nothing to it and never unlinks (the sim owns the name). */
void hal_close(void);

/* 0 for CLOSED (and OK), otherwise the status's own value: 2 timeout, 3 handshake timeout,
 * 4 bad layout, 5 protocol, 6 error. Exit 1 is left to the program's own argument errors. */
int hal_exit_status(astra_hal_status status);

#ifdef __cplusplus
}
#endif

#endif /* ASTRA_HAL_H */
