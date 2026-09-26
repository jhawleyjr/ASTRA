/*
 * The shm region layout and the HAL API under a C11 compiler that is not the sim's (ADR-0024: the
 * layout header compiles as C11 and C++17). ctest compiles this with gcc and clang, -std=c11
 * -Wpedantic -Werror -fsyntax-only; a failing assert is a compile error, which is the test.
 * offsetof is used here (the test may include <stddef.h>; the header itself may not).
 */
#include <stddef.h>

#include "embedded/hal/astra_hal.h"
#include "embedded/hal/astra_shm_layout.h"

_Static_assert(ASTRA_SHM_MAGIC == 0x4153484Du, "magic");
_Static_assert(ASTRA_SHM_LAYOUT_VERSION == 1u, "layout version");
_Static_assert(ASTRA_SHM_SEQ_INIT == 0xFFFFFFFFu, "sequence sentinel");
_Static_assert(ASTRA_SHM_CLOSE_SEQ == 0xFFFFFFFEu, "close sequence");
_Static_assert(ASTRA_SHM_SIM_INIT == 0u, "a zero-filled segment reads INIT");
_Static_assert(ASTRA_SHM_PEER_NONE == 0u, "a zero-filled segment reads NONE");

_Static_assert(sizeof(astra_shm_header_v1) == 64, "header size");
_Static_assert(_Alignof(astra_shm_header_v1) == 8, "header alignment");
_Static_assert(offsetof(astra_shm_header_v1, magic) == 0, "magic");
_Static_assert(offsetof(astra_shm_header_v1, layout_version) == 4, "layout_version");
_Static_assert(offsetof(astra_shm_header_v1, layout_size) == 8, "layout_size");
_Static_assert(offsetof(astra_shm_header_v1, icd_version) == 12, "icd_version");
_Static_assert(offsetof(astra_shm_header_v1, sim_state) == 16, "sim_state");
_Static_assert(offsetof(astra_shm_header_v1, peer_state) == 20, "peer_state");
_Static_assert(offsetof(astra_shm_header_v1, req_seq) == 24, "req_seq");
_Static_assert(offsetof(astra_shm_header_v1, ack_seq) == 28, "ack_seq");
_Static_assert(offsetof(astra_shm_header_v1, sim_pid) == 32, "sim_pid");
_Static_assert(offsetof(astra_shm_header_v1, peer_pid) == 36, "peer_pid");
_Static_assert(offsetof(astra_shm_header_v1, reserved) == 40, "reserved");

_Static_assert(sizeof(astra_shm_region_v1) == 328, "region size");
_Static_assert(_Alignof(astra_shm_region_v1) == 8, "region alignment");
_Static_assert(offsetof(astra_shm_region_v1, header) == 0, "header");
_Static_assert(offsetof(astra_shm_region_v1, request) == 64, "request");
_Static_assert(offsetof(astra_shm_region_v1, reply) == 264, "reply");

_Static_assert(ASTRA_HAL_OK == 0 && ASTRA_HAL_CLOSED == 1 && ASTRA_HAL_TIMEOUT == 2 &&
                   ASTRA_HAL_HANDSHAKE_TIMEOUT == 3 && ASTRA_HAL_BAD_LAYOUT == 4 &&
                   ASTRA_HAL_PROTOCOL == 5 && ASTRA_HAL_ERROR == 6,
               "HAL status values are the exit statuses (plan Q-3)");
