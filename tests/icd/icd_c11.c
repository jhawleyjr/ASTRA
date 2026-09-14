/*
 * The ICD layout under a C11 compiler that is not the sim's (ADR-0006). ctest compiles this with
 * `gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only` and again with clang -std=c11; a
 * failing assert is a compile error, which is the test. Mirrors tests/unit/icd/icd_test.cpp.
 */
#include <stddef.h>

#include "icd/astra_icd.h"

_Static_assert(ASTRA_ICD_VERSION == 1u, "version");
_Static_assert(ASTRA_ICD_MAGIC == 0x41535452u, "magic");

_Static_assert(sizeof(astra_sim_to_sw_v1) == 200, "sim_to_sw size");
_Static_assert(_Alignof(astra_sim_to_sw_v1) == 8, "sim_to_sw alignment");
_Static_assert(offsetof(astra_sim_to_sw_v1, magic) == 0, "magic");
_Static_assert(offsetof(astra_sim_to_sw_v1, version) == 4, "version");
_Static_assert(offsetof(astra_sim_to_sw_v1, frame) == 8, "frame");
_Static_assert(offsetof(astra_sim_to_sw_v1, t) == 16, "t");
_Static_assert(offsetof(astra_sim_to_sw_v1, phi) == 24, "phi");
_Static_assert(offsetof(astra_sim_to_sw_v1, theta) == 32, "theta");
_Static_assert(offsetof(astra_sim_to_sw_v1, psi) == 40, "psi");
_Static_assert(offsetof(astra_sim_to_sw_v1, p) == 48, "p");
_Static_assert(offsetof(astra_sim_to_sw_v1, q) == 56, "q");
_Static_assert(offsetof(astra_sim_to_sw_v1, r) == 64, "r");
_Static_assert(offsetof(astra_sim_to_sw_v1, u) == 72, "u");
_Static_assert(offsetof(astra_sim_to_sw_v1, v) == 80, "v");
_Static_assert(offsetof(astra_sim_to_sw_v1, w) == 88, "w");
_Static_assert(offsetof(astra_sim_to_sw_v1, alpha) == 96, "alpha");
_Static_assert(offsetof(astra_sim_to_sw_v1, beta) == 104, "beta");
_Static_assert(offsetof(astra_sim_to_sw_v1, tas) == 112, "tas");
_Static_assert(offsetof(astra_sim_to_sw_v1, cas) == 120, "cas");
_Static_assert(offsetof(astra_sim_to_sw_v1, mach) == 128, "mach");
_Static_assert(offsetof(astra_sim_to_sw_v1, altitude) == 136, "altitude");
_Static_assert(offsetof(astra_sim_to_sw_v1, vertical_speed) == 144, "vertical_speed");
_Static_assert(offsetof(astra_sim_to_sw_v1, lat) == 152, "lat");
_Static_assert(offsetof(astra_sim_to_sw_v1, lon) == 160, "lon");
_Static_assert(offsetof(astra_sim_to_sw_v1, ax) == 168, "ax");
_Static_assert(offsetof(astra_sim_to_sw_v1, ay) == 176, "ay");
_Static_assert(offsetof(astra_sim_to_sw_v1, az) == 184, "az");
_Static_assert(offsetof(astra_sim_to_sw_v1, status_flags) == 192, "status_flags");
_Static_assert(offsetof(astra_sim_to_sw_v1, reserved) == 196, "reserved");

_Static_assert(sizeof(astra_sw_to_sim_v1) == 64, "sw_to_sim size");
_Static_assert(_Alignof(astra_sw_to_sim_v1) == 8, "sw_to_sim alignment");
_Static_assert(offsetof(astra_sw_to_sim_v1, magic) == 0, "magic");
_Static_assert(offsetof(astra_sw_to_sim_v1, version) == 4, "version");
_Static_assert(offsetof(astra_sw_to_sim_v1, frame) == 8, "frame");
_Static_assert(offsetof(astra_sw_to_sim_v1, elevator) == 16, "elevator");
_Static_assert(offsetof(astra_sw_to_sim_v1, aileron) == 24, "aileron");
_Static_assert(offsetof(astra_sw_to_sim_v1, rudder) == 32, "rudder");
_Static_assert(offsetof(astra_sw_to_sim_v1, throttle) == 40, "throttle");
_Static_assert(sizeof(((astra_sw_to_sim_v1*)0)->throttle) == 16, "throttle size");
_Static_assert(offsetof(astra_sw_to_sim_v1, mode_flags) == 56, "mode_flags");
_Static_assert(offsetof(astra_sw_to_sim_v1, fault_flags) == 60, "fault_flags");

int main(void) { return 0; }
