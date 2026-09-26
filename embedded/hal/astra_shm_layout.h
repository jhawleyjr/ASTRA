/*
 * The shared-memory lockstep region (ADR-0024; ADR-0007 as amended; docs/architecture.md §10, §11).
 *
 * The ONE definition of the region layout and protocol constants, included by the sim-side shm
 * transport plugin (plugins/sim/transport/shm) and by the HAL's shm backend (embedded/hal/shm). It
 * is an include, not a link dependency, so ADR-0008's rule holds: embedded/ links only icd/ and
 * embedded/hal/. C11 and C++17; <stdint.h> and icd/astra_icd.h only; data and constants, no
 * functions. Any change to this file bumps ASTRA_SHM_LAYOUT_VERSION, which the peer validates in
 * the handshake; it never edits an ICD struct.
 *
 * Protocol (ADR-0024, M4 PR 4 plan docs/plans/m4-pr4-shm-transport.md):
 *
 *   Ownership. Every word has exactly one writer. The sim creates the segment (O_EXCL), sizes it,
 *   and initializes the WHOLE header -- including the peer-owned words, to their initial values --
 *   before its release-store of sim_state = SIM_READY, and never writes a peer-owned word again.
 *   After SIM_READY the sim alone writes sim_state, req_seq, and request; after PEER_READY the peer
 *   alone writes peer_state, ack_seq, and reply (peer_pid is written before PEER_READY).
 *
 *   Ordering. A writer stores the payload, then release-stores the word that publishes it, then
 *   FUTEX_WAKEs that word; a reader acquire-loads the word before reading the payload. Futexes are
 *   process-shared (no FUTEX_PRIVATE_FLAG). A waiter always passes the value it last observed as
 *   the futex expected value, and a writer always changes a word before waking it, so a store that
 *   lands between a waiter's check and its FUTEX_WAIT makes the wait return at once (no lost wake).
 *
 *   Sequence words. req_seq and ack_seq start at ASTRA_SHM_SEQ_INIT, so frame 0 is distinguishable
 *   from a fresh word. The sim publishes req_seq = (uint32_t)frame; the peer answers with ack_seq =
 *   the same value. ASTRA_SHM_CLOSE_SEQ is never a frame (the sim halts before publishing
 *   frame >= ASTRA_SHM_CLOSE_SEQ, ADR-0022).
 *
 *   Handshake. The peer retries shm_open until the handshake deadline, never maps a segment smaller
 *   than sizeof(astra_shm_region_v1), acquire-polls sim_state until SIM_READY, and only then
 *   validates magic, layout_version, layout_size, and icd_version; then it release-stores
 *   peer_state = PEER_READY. The sim waits for PEER_READY before frame 0.
 *
 *   Shutdown. On NORMAL completion the sim release-stores sim_state = SIM_CLOSED, then req_seq =
 *   ASTRA_SHM_CLOSE_SEQ, then wakes req_seq and sim_state; the peer treats either as a clean end
 *   (exit 0), and close takes precedence over its deadline expiry. After a sim failure nothing is
 *   written: the peer ends at its own peer_timeout_ms (plan Q-3). The creator unlinks the name.
 */
#ifndef ASTRA_SHM_LAYOUT_H
#define ASTRA_SHM_LAYOUT_H

#include <stdint.h>

#include "icd/astra_icd.h"

#define ASTRA_SHM_MAGIC 0x4153484Du /* "ASHM" */
#define ASTRA_SHM_LAYOUT_VERSION 1u

#define ASTRA_SHM_SEQ_INIT 0xFFFFFFFFu  /* both sequence words start here */
#define ASTRA_SHM_CLOSE_SEQ 0xFFFFFFFEu /* req_seq at normal shutdown; never a request */

/* sim_state values. INIT is 0 so a segment ftruncate has zero-filled but the sim has not finished
 * reads as not ready. */
#define ASTRA_SHM_SIM_INIT 0u
#define ASTRA_SHM_SIM_READY 1u
#define ASTRA_SHM_SIM_CLOSED 2u

/* peer_state values. */
#define ASTRA_SHM_PEER_NONE 0u
#define ASTRA_SHM_PEER_READY 1u

typedef struct {
  /* Written by the sim before SIM_READY, immutable afterwards. layout_size is
   * sizeof(astra_shm_region_v1); icd_version is ASTRA_ICD_VERSION. */
  uint32_t magic, layout_version, layout_size, icd_version;
  uint32_t sim_state;   /* futex word; writer: sim */
  uint32_t peer_state;  /* futex word; writer: peer (initialized by the sim before SIM_READY) */
  uint32_t req_seq;     /* futex word; writer: sim */
  uint32_t ack_seq;     /* futex word; writer: peer (initialized by the sim before SIM_READY) */
  int32_t sim_pid;      /* diagnostic only: no pid probing (ADR-0024) */
  int32_t peer_pid;     /* diagnostic only; written by the peer before PEER_READY */
  uint64_t reserved[3]; /* 0; uint64_t so the header is itself 8-byte aligned */
} astra_shm_header_v1;

typedef struct {
  astra_shm_header_v1 header; /* offset 0 */
  astra_sim_to_sw_v1 request; /* offset 64; writer: sim */
  astra_sw_to_sim_v1 reply;   /* offset 264; writer: peer */
} astra_shm_region_v1;

/* The member sizes sum to exactly these totals, so with 8-byte alignment there is no padding and
 * every offset above is pinned (offsetof would need <stddef.h>, outside this header's include
 * rule; tests/lockstep/shm_layout_c11.c checks the offsets directly). */
ASTRA_ICD_STATIC_ASSERT(sizeof(astra_shm_header_v1) == 64, "shm layout changed: bump version");
ASTRA_ICD_STATIC_ASSERT(sizeof(astra_shm_region_v1) == 328, "shm layout changed: bump version");
ASTRA_ICD_STATIC_ASSERT(ASTRA_ICD_ALIGNOF(astra_shm_header_v1) == 8, "shm header alignment");
ASTRA_ICD_STATIC_ASSERT(ASTRA_ICD_ALIGNOF(astra_shm_region_v1) == 8, "shm region alignment");

#endif /* ASTRA_SHM_LAYOUT_H */
