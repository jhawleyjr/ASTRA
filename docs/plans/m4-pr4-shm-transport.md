# Implementation plan: M4 PR 4, shm transport and HAL shm backend

Spec/brief: [M4 Lockstep plan](m4-lockstep.md), PR 4 row, "Contract and failure behavior", R4, R5, R10,
A4; [ADR-0007](../decisions/0007-shared-memory-lockstep-futex.md) as amended by
[ADR-0024](../decisions/0024-shm-layout-header-and-protocol.md);
[ADR-0022](../decisions/0022-lockstep-exchange-stage.md);
[ADR-0023](../decisions/0023-transport-timeout-clock-exception.md); [ADR-0008](../decisions/0008-monorepo-embedded-separate-cmake.md);
PR 3's `Transport` contract (`core/include/astra/interfaces/transport.hpp`, [PR 3 plan](m4-pr3-core-seam.md)).
Risk: Tier 3 (cross-process concurrency, futex memory ordering, a transport, health/halt behavior).
Status: approved
Plan approval: **approved by Joe, 2026-09-25, with Q-1 to Q-8 as recommended plus three changes and four
nits** (recorded under "Plan critique and decisions" and folded into the text below). His task statement
(2026-09-25) fixes scope, tests, and acceptance.
Base: `origin/main` = `7a349ee` (#54's merge, confirmed 2026-09-25), branch `worktree-m4-pr4-shm-transport`.
Correction rounds 0/2.

## Outcome and non-goals

**Outcome.** A config with `transport.plugin: plugins/sim/transport/shm` runs the lockstep exchange against
a separate process built only from `embedded/hal/` and `icd/`, over one shared-memory region whose layout is
defined once in `embedded/hal/astra_shm_layout.h`. Waits are futex waits with one absolute
`CLOCK_MONOTONIC` deadline, at exactly ADR-0023's two clock sites. The handshake, `CLOSED`/`CLOSE_SEQ`
shutdown, and segment ownership follow ADR-0024. A two-process echo peer proves it, and a Release
1e6-exchange stress test measures the per-exchange cost.

**Non-goals.** No autopilot, no `embedded/CMakeLists.txt` or `project()`, no `just embedded-standalone`
recipe, no `nm` autopilot-boundary test (all PR 5). No Python driver, no lockstep acceptance runs, no
`cruise_*_turbulence.yaml` (all PR 6). No PR 3 follow-up C. No `core/` change: the `Transport` interface,
`LockstepExchange`, and `kCoreApiVersion` stay as PR 3 left them. No ICD struct change, no golden,
tolerance, ADR, or architecture edit.

**Invariants.** PR 3's tests and the M3 frozen regression pass unmodified. The per-exchange path on both sides
(`publish`, `wait_ack`, `read_reply`, `hal_wait_frame`, `hal_send`, `hal_now`) is allocation-free and
exception-free and builds no `std::string`. Every shared word has one writer after initialization. Clock
reads occur only in the two named functions and never reach state, signals, ICD fields, or the recorder.

## Repository findings (observed at `7a349ee`)

- `Transport` (`core/include/astra/interfaces/transport.hpp`) is a `Model`. `open(settings)` returns the
  initial ack word, `publish(request, seq)` and `wait_ack(unchanged)` are noexcept, and `close(CloseReason)`
  is noexcept. `AckStatus` has three values: `kObserved` (may equal `unchanged`), `kExpired`, and
  `kPeerClosed`. The contract text pins the deadline: "Every call between two publish() calls shares ONE
  absolute deadline, fixed at the first such call". The clock read is "confined to this one function".
- `LockstepExchange::receive` (`core/src/kernel/lockstep_exchange.cpp:232`) loops on `wait_ack(last_ack_)`
  until `kExpired`, `kPeerClosed`, or a changed ack. Termination depends only on the transport's deadline.
  Its destructor calls `close(close_reason())` only when `open()` succeeded (`opened_`). A throwing
  `open()` therefore gets no `close()`, so the plugin has to clean up on its own failure path.
  `close_reason()` is `kAfterFailure` once any halt is stored.
- `transport.*` keys: `LockstepExchange` reads the sim-owned keys and passes the whole block to the plugin's
  `Model::init`. The loopback reads `transport.peer`. Unconsumed keys fail `Config::unknown_keys()`
  (`simulation.cpp:470`), so the shm plugin's own key must be read in its `init`.
- Plugins get the repo root on the include path through `astra_core_headers -> astra::icd`
  (`core/CMakeLists.txt:29`, `icd/CMakeLists.txt`). A plugin can therefore
  `#include "embedded/hal/astra_shm_layout.h"` with no link change, the include-only relation ADR-0024
  requires. `astra_build_flags` (warnings, `-Werror`, sanitizers) reaches plugins the same way.
- No `embedded/` directory exists. The top level is `project(ASTRA LANGUAGES CXX)` with
  `CMAKE_CXX_STANDARD 23` global. A C++17 target needs a per-target `CXX_STANDARD 17`. C11 checks run as
  `add_test` compile commands (`tests/CMakeLists.txt:21-29`, `icd_c11.c`), not as a C language target.
- `astra_add_plugin` accepts only `SOURCES`/`NO_LOAD_ALL`. The static-plugins preset links every plugin
  into `libastra_core` (glibc 2.39: `shm_open` lives in libc, so no `-lrt`). `AllPlugins` loads every
  plugin without calling `init`.
- Tests: `astra_unit_tests` (label `unit`, the Stop hook's subset), `astra_physics_tests` (label `physics`,
  links `tests/support/alloc_counter.cpp`, home of `NoAlloc.*`). `gtest_discover_tests` runs each case in
  its own process, so the test pid is unique per case.
- `.clang-tidy` checks every TU in the asan compile database, `embedded/` included, but applies
  `HeaderFilterRegex` only to `core|plugins|aircraft|tests` `.hpp`. `cppcoreguidelines-no-malloc` is on.
- LeakSanitizer under ptrace (lesson 2026-09-17) can stall the asan phase in agent sessions.

## Design

### Shared layout header (`embedded/hal/astra_shm_layout.h`)

C11 and C++17. Includes only `<stdint.h>` and `icd/astra_icd.h`. Static-asserted with the ICD's
`ASTRA_ICD_STATIC_ASSERT` macros. It holds data and constants only, with no functions.

```c
#define ASTRA_SHM_MAGIC          0x4153484Du  /* "ASHM" */
#define ASTRA_SHM_LAYOUT_VERSION 1u
#define ASTRA_SHM_SEQ_INIT       0xFFFFFFFFu  /* both sequence words start here (ADR-0024) */
#define ASTRA_SHM_CLOSE_SEQ      0xFFFFFFFEu  /* req_seq at shutdown; never a request */
#define ASTRA_SHM_SIM_INIT 0u   /* zero-filled by ftruncate, so an unfinished segment reads INIT */
#define ASTRA_SHM_SIM_READY 1u
#define ASTRA_SHM_SIM_CLOSED 2u
#define ASTRA_SHM_PEER_NONE 0u
#define ASTRA_SHM_PEER_READY 1u
typedef struct {                /* 64 bytes */
  uint32_t magic, layout_version, layout_size, icd_version;  /* sim, before SIM_READY; then immutable */
  uint32_t sim_state;           /* futex word, writer: sim */
  uint32_t peer_state;          /* futex word, writer: peer */
  uint32_t req_seq;             /* futex word, writer: sim (initialized by the sim before SIM_READY) */
  uint32_t ack_seq;             /* futex word, writer: peer (initialized by the sim before SIM_READY) */
  int32_t sim_pid, peer_pid;    /* diagnostic only (ADR-0024: no pid probing) */
  uint64_t reserved[3];         /* 0; uint64_t so the header itself is 8-byte aligned (critique F-7) */
} astra_shm_header_v1;
typedef struct {                /* 328 bytes */
  astra_shm_header_v1 header;   /* offset 0 */
  astra_sim_to_sw_v1 request;   /* offset 64, writer: sim */
  astra_sw_to_sim_v1 reply;     /* offset 264, writer: peer */
} astra_shm_region_v1;
```

`sizeof` asserts on both structs (64 and 328) plus `_Alignof == 8` on both pin every offset. The members sum
to exactly 328 and leave no room for padding. A header of 32-bit words alone would have alignment 4, which
the draft missed (critique F-7). `<stddef.h>`/`offsetof` would break the include rule.
`layout_size = sizeof(astra_shm_region_v1)`. The header carries `icd_version` in addition to ADR-0024's
magic, version, and size, so a peer built against another ICD fails at the handshake.

**Single-writer table.** After `SIM_READY`, the sim alone writes `sim_state`, `req_seq`, and `request`.
Before `PEER_READY` the peer writes `peer_pid`; after that it alone writes `peer_state`, `ack_seq`, and
`reply`. The sim initializes the peer-owned `ack_seq`, `peer_state`, and `peer_pid` to their initial
values before its release-store of `SIM_READY` and never touches them again. That is ADR-0024's
"initializes the whole header", followed by an ownership hand-off that the acquire of `SIM_READY`
publishes.

**Ordering vocabulary.** Both sides use `__atomic_load_n(p, __ATOMIC_ACQUIRE)` and
`__atomic_store_n(p, v, __ATOMIC_RELEASE)` on the `uint32_t` words. These are GCC/Clang builtins, valid
in C++17 and C++23, so both sides use the same vocabulary. Each side wraps them in two small inline helpers,
guarded by a static assert of `__atomic_always_lock_free(4, 0)`. Payloads are plain `memcpy`, ordered by
those release/acquire pairs. Futex calls use `syscall(SYS_futex, ...)` without `FUTEX_PRIVATE_FLAG`
(process-shared). Waits use `FUTEX_WAIT_BITSET` with `FUTEX_BITSET_MATCH_ANY` and an absolute
`CLOCK_MONOTONIC` timespec (no `FUTEX_CLOCK_REALTIME`). Wakes use `FUTEX_WAKE` for `INT_MAX` waiters.

| Transition | Writer does | Reader does |
|---|---|---|
| Ready (sim) | header init, then release `sim_state = SIM_READY`, wake `sim_state` | acquire `sim_state`, then read header |
| Ready (peer) | `peer_pid`, then release `peer_state = PEER_READY`, wake `peer_state` | acquire `peer_state` |
| Request | `memcpy request`, then release `req_seq = seq`, wake `req_seq` | acquire `req_seq`, then `memcpy` out |
| Reply | `memcpy reply`, then release `ack_seq = seq`, wake `ack_seq` | acquire `ack_seq`, then `memcpy` out |
| Close | release `sim_state = CLOSED`, then release `req_seq = CLOSE_SEQ`, then wake `req_seq` and `sim_state` | acquire `req_seq` and `sim_state` on every wake and before classifying expiry |

**Lost-wake argument, one rule applied everywhere.** Every waiter passes the value it last observed as the
futex `expected` value. Every writer changes the word before it wakes. The kernel compares under its hash
bucket lock, so a store that lands between the waiter's check and its `FUTEX_WAIT` makes the wait return
`EAGAIN` immediately rather than sleep. The one place this rule was broken in the ADR drafts, a close
signalled on a word the HAL does not wait on, is ADR-0024's `CLOSE_SEQ` fix. That exact interleaving is
what the hook test pins.

### The single clock function per side (ADR-0023)

- **Sim:** `ShmTransport::wait_until(const std::uint32_t* word, std::uint32_t expected, Deadline& d)`
  returns `WaitResult{kind, value}`. It lives in its own TU, `plugins/sim/transport/shm/shm_wait.cpp`. It is
  the only function in the plugin that reads a clock or issues a futex wait. `open()` (the handshake) and
  `wait_ack()` both call it.
- **HAL:** `astra_hal_shm_wait_until(const uint32_t* word, uint32_t expected, astra_hal_deadline* d)` has
  the same algorithm. It lives in its own TU, `embedded/hal/shm/hal_shm_wait.cpp`, with `extern "C"`
  linkage. It adds a `word == NULL` form for the attach loop before a mapping exists:
  `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, min(now + 1 ms, deadline))`. It is the only HAL function
  that reads a clock.

**Algorithm, both sides (critique F-1, F-2).** An absolute futex timeout alone does not bound the loop. In
Linux's `__futex_wait` both the value compare (`EAGAIN`) and a concurrent `FUTEX_WAKE` (return 0) take
precedence over `ETIMEDOUT`, so a peer that keeps waking can keep a waiter returning after its deadline. The
deadline is therefore **latched by an explicit clock comparison**:

1. If `d.expired` is latched, return `kExpired` at once, with no syscall.
2. If `!d.armed`, set `d.abs = now + timeout` (the first clock read) and arm.
3. Issue one `FUTEX_WAIT_BITSET(word, expected, d.abs)`, unless `word` is `NULL`, in which case sleep as
   above.
4. **Acquire-load `*word` into `value`, then read the clock.** If the syscall returned `ETIMEDOUT`, or
   `now >= d.abs`, latch `d.expired` and return `{kExpired, value}`.
5. If the syscall failed with anything other than `EAGAIN` or `EINTR` (for example `EFAULT` or `ENOSYS`,
   impossible on a valid mapping), latch and return `{kFault, value}`.
6. Otherwise return `{kReturned, value}`.

Loading before the clock read means that any value reported as `kReturned` was observed at a time before
the deadline. A change that lands after the deadline, or one first observed after it (the waiter was
descheduled), is expiry. Every re-wait uses the same `d.abs`. Only `publish()` (sim) or a new
`hal_wait_frame` call (HAL) re-arms. A caller can therefore make at most one futex call after the deadline
before the latch returns expiry without a syscall, and core's loop terminates. The second clock read is
ADR-0023's "time remaining on a re-wait", inside the same function.

### `plugins/sim/transport/shm` (new plugin; `shm.cpp`, `shm_wait.cpp`, `shm_transport.hpp`)

- `init(config)` reads **`transport.segment`** (Q-5), a required string that must match
  `^astra_[0-9]{1,10}_[A-Za-z0-9]{1,32}$` (ADR-0024's `astra_<driver pid>_<nonce>`). A missing or
  malformed value is a `ConfigError` at `transport.segment`. The plugin adds the leading `/` for
  `shm_open`. `init` touches no shared memory.
- `open(settings)` runs the handshake. It calls `shm_open(O_CREAT|O_EXCL|O_RDWR, 0600)`; `EEXIST` means a
  stale or foreign segment and throws naming the segment, which it neither unlinks nor touches (ADR-0024:
  no unlink-on-next-start). Then `ftruncate(328)`, `mmap(MAP_SHARED)`, `close(fd)`, and header init
  (`magic`, version, size, `icd_version`, both seqs `SEQ_INIT`, `peer_state = NONE`, pids; payloads are
  already zero). Then release `sim_state = SIM_READY` and wake. It waits for `PEER_READY` through
  `wait_until(&peer_state, NONE, handshake_deadline)` with its own deadline of `handshake_timeout_ms`
  (ADR-0024: "same handshake budget, separate wait"). Next it acquire-loads `ack_seq`; a value other than
  `SEQ_INIT` throws as a protocol violation. It **unlinks the name** (Q-6) and returns `SEQ_INIT`.
  **Three separately owned resources (critique F-3):**
  - the descriptor, a scoped owner from a successful `shm_open` until the post-`mmap` `close`;
  - the mapping, valid only after a successful `mmap`;
  - the name, owned only after an `O_EXCL` create succeeds, never for `EEXIST`.

  Every throw path after `shm_open` closes a live descriptor, unmaps only a valid mapping, and unlinks only
  an owned name. Core does not call `close()` after a failed `open()`, so this path has to be complete on its
  own. Tests inject `ftruncate` and `mmap` failures (below).
- `publish(request, seq)`: `memcpy` into `region->request`, release `req_seq = seq`, `FUTEX_WAKE(req_seq)`,
  and disarm the ack deadline (`ack_deadline_.armed = false`). It reads no clock.
- `wait_ack(unchanged)` calls `wait_until(&ack_seq, unchanged, ack_deadline_)`. On the first call after a
  publish this arms the deadline (Q-2). The result maps as follows:
  - `kExpired` or `kFault`: return `kExpired`, **whatever value was loaded**. An ack observed after the
    deadline is a late ack, which halts (`transport.hpp`: "a call after that deadline returns kExpired";
    M4 R5: "a late ack" halts). A fault is terminal (critique F-2).
  - `kReturned`: return `{kObserved, value}` verbatim. The value may equal `unchanged`.

  The plugin never classifies an ack: equal, `seq`, or out of order is core's decision. It never returns
  `kPeerClosed` (Q-7).
- `read_reply(reply)`: `memcpy` all 64 bytes from `region->reply`. It is ordered by the acquire load in the
  `wait_ack` call that returned `seq` on the same thread.
- `close(kNormal)` (Q-3): release `sim_state = CLOSED`, release `req_seq = CLOSE_SEQ`, wake `req_seq`, wake
  `sim_state`, `munmap`, and `shm_unlink` (idempotent; `ENOENT` ignored). `close(kAfterFailure)` writes
  **nothing** to the region: `munmap` and `shm_unlink` only. `close` is idempotent, and the destructor
  performs `close(kAfterFailure)` if neither `close` nor the failure path ran.
- `release_after_failure()` is the `open()` throw path: `munmap` and unlink, no wire write, the same as
  `kAfterFailure`.

### `embedded/hal` (C API, C++17 implementation, static library `astra_hal`)

`embedded/hal/astra_hal.h` is valid C11 and C++17 (`extern "C"`) and includes only `<stdint.h>` and
`icd/astra_icd.h`:

```c
typedef enum {
  ASTRA_HAL_OK = 0, ASTRA_HAL_CLOSED = 1, ASTRA_HAL_TIMEOUT = 2, ASTRA_HAL_HANDSHAKE_TIMEOUT = 3,
  ASTRA_HAL_BAD_LAYOUT = 4, ASTRA_HAL_PROTOCOL = 5, ASTRA_HAL_ERROR = 6
} astra_hal_status;
typedef struct {
  const char* segment;             /* astra_<pid>_<nonce>, from the driver */
  uint32_t handshake_timeout_ms;   /* driver-supplied */
  uint32_t peer_timeout_ms;        /* driver-supplied, >= the sim's timeout_ms (ADR-0024) */
} astra_hal_config;
astra_hal_status hal_init(const astra_hal_config* config);   /* the peer's handshake */
astra_hal_status hal_wait_frame(astra_sim_to_sw_v1* in);     /* blocks for the next request */
astra_hal_status hal_send(const astra_sw_to_sim_v1* out);    /* publishes the reply */
double hal_now(void);          /* t of the last frame received (frame-derived); 0.0 before any */
void hal_close(void);          /* munmap; writes nothing to the region; never unlinks */
int hal_exit_status(astra_hal_status status);                /* the table under Q-3 */
```

These are architecture §11's three calls plus init, close, and exit status. The HAL keeps module-static
state (one session per process), and `hal_close` resets it so tests can init again.

- `hal_init` runs under one handshake deadline over every step (ADR-0024):
  1. Retry `shm_open(O_RDWR)` while `ENOENT`. Between attempts it calls `wait_until(NULL, ...)`, which
     sleeps 1 ms.
  2. `fstat`. While the size is below 328 it closes the fd, sleeps, and retries. It never maps a short
     segment.
  3. `mmap`, then `close(fd)`.
  4. Acquire-load `sim_state`. `SIM_CLOSED` returns **`ASTRA_HAL_PROTOCOL`** (Joe's change 1: a close before
     the peer's `PEER_READY` is unreachable in a correct run), checked before any validation. Otherwise,
     until the state is `SIM_READY`, it runs `wait_until(&sim_state, observed, d)`, rechecking `CLOSED`
     after every return.
  5. Validate magic, `layout_version`, `layout_size`, and `icd_version`. A mismatch returns
     `ASTRA_HAL_BAD_LAYOUT` at once, with no retry.
  6. Acquire-load `req_seq`. A value other than `SEQ_INIT` means a stale or foreign session and returns
     `ASTRA_HAL_PROTOCOL`. A live sim cannot publish before `PEER_READY`.
  7. Write `peer_pid`, release `peer_state = PEER_READY`, and wake it.

  Expiry at any step returns `ASTRA_HAL_HANDSHAKE_TIMEOUT`.
- `hal_wait_frame` returns `ASTRA_HAL_ERROR` at once if a reply is still pending (a `hal_wait_frame` with
  no `hal_send` since the last frame; Joe's nit). Otherwise it works against a deadline of `peer_timeout_ms`,
  armed at its first wait, and loops:
  1. Acquire `req_seq` and `sim_state`. `CLOSE_SEQ` or `CLOSED` returns `ASTRA_HAL_CLOSED`.
  2. If `req_seq` differs from the last request, it copies `request` and validates magic, version, and
     `request.frame == (uint64_t)req_seq`, the full 64-bit frame (Joe's nit; a mismatch returns
     `ASTRA_HAL_PROTOCOL`). It records the
     sequence and `t`, marks a reply pending, and returns `ASTRA_HAL_OK`.
  3. Otherwise it calls `wait_until(&req_seq, last, d)`. On `kExpired` or `kFault` it acquire-loads
     `req_seq` and `sim_state` once more. `CLOSE_SEQ` or `CLOSED` returns `ASTRA_HAL_CLOSED` (ADR-0024:
     close beats expiry). **Anything else** returns `ASTRA_HAL_TIMEOUT` (or `ASTRA_HAL_ERROR` for
     `kFault`), including an ordinary request observed after the deadline: only close has precedence over
     expiry (critique F-2). On `kReturned` it loops to step 1, where close is checked first and a new
     request is detected from the helper's pre-clock value.
- `hal_send` returns `ASTRA_HAL_ERROR` if no reply is pending. Otherwise it `memcpy`s the reply,
  release-stores `ack_seq` = **the pending request's sequence** (never `out->frame`, so a wrong echo stays
  visible to the sim's `reply.frame` check), wakes, and clears the pending flag.

### Echo peer (`tests/lockstep/echo_peer.cpp`, executable `astra_echo_peer`)

C++17, linked to `astra_hal` only (plus `astra_build_flags`, compile/link options, not a library). The CLI
is `--segment S --handshake-timeout-ms N --peer-timeout-ms N` plus these options:

- `--mode hold --commands e,a,r,t` sends constant commands, for `Simulation` tests.
- `--mode echo` derives the reply from the request (`elevator = phi`, `aileron = theta`, `rudder = psi`,
  `throttle = {p, q}`, `mode_flags = FNV-1a32 of the 200 request bytes`, `fault_flags = low 32 bits of
  request.frame XOR status_flags`). The stress test fills **every** variable request field (all 21 doubles
  and `status_flags`) with a distinct frame-dependent pattern. For every exchange it builds the complete
  expected 64-byte reply independently and compares bytes (critique F-6), so a stale or torn payload in
  either direction fails. This is detection, not a proof of memory ordering (M4 Q14).
- `--notify-fd FD` together with `--stop-after N` (after N replies: write one byte, then `pause()` to be
  SIGKILLed) or `--abort-after N` (write one byte, then `abort()`).

The peer zeroes each reply before filling it (ADR-0022). On exit it prints `echo_peer: handled <n>
status <s>` to stdout and exits with `hal_exit_status`. It is test support, not product: it lives under
`tests/`, not `embedded/`, so PR 5's standalone project does not inherit it.

### Q-1: How `embedded/hal` builds in the main tree without preempting PR 5

**Recommend:** PR 4 adds `embedded/hal/CMakeLists.txt` (`add_library(astra_hal STATIC ...)`, per-target
`CXX_STANDARD 17`, `CXX_EXTENSIONS OFF`, `PUBLIC astra::icd`, `PRIVATE astra_build_flags`) and **one line**
in the top-level `CMakeLists.txt`, `add_subdirectory(embedded/hal)` after `add_subdirectory(icd)`, with a
comment that PR 5 replaces it with `add_subdirectory(embedded)`. PR 4 does not create
`embedded/CMakeLists.txt`, a `project()`, or a standalone recipe. The HAL CMakeLists assumes the in-tree
`astra::icd` and `astra_build_flags` targets. Making it standalone-capable (defining or finding both when
absent) is PR 5's work under ADR-0008.

The shm plugin includes `embedded/hal/astra_shm_layout.h` through the existing repo-root include and never
links `astra_hal` (ADR-0024: an include, not a link). The boundary test `EchoPeer_links_no_astra_core`
(CMake `-P` script) asserts that the echo peer's `readelf -d` NEEDED list has no `libastra_core` and that
`nm -u` shows no `_ZN5astra` symbol. It uses the existing `check_undefined_symbol.cmake` style. The
autopilot's `nm` test stays PR 5's.

**Alternative:** create a minimal `embedded/CMakeLists.txt` with `project()` now. It is rejected because it
fixes PR 5's standalone shape (the icd target and flags) before PR 5 designs it.

### Q-2: Where the per-publication deadline is fixed

**Recommend:** at the first `wait_ack()` after each `publish()`, inside `wait_until`, the one clock
function. `publish()` only disarms. That is the contract PR 3 already wrote into `transport.hpp`, and it
keeps the clock read in one function (ADR-0023). The HAL does the same per `hal_wait_frame` call. The timing
difference from arming in `publish()` is one `FUTEX_WAKE` syscall (microseconds).

**Alternative:** arm in `publish()`. It is rejected because it adds a second clock-reading function, or
routes `publish()` through the wait helper, and contradicts PR 3's pinned text.

### Q-3: What `close(kAfterFailure)` does on the wire, and the HAL exit status per case

**Recommend (B, silent failure close):** `kAfterFailure` writes nothing to the region. It unmaps and
unlinks only. `CLOSED`/`CLOSE_SEQ` stays exactly ADR-0024's "normal completion", so a peer's exit 0 is an
independent witness that the sim finished normally, which PR 6's two-counter R1 can use. After a sim
failure the peer ends at its own `peer_timeout_ms` (exit 2) or when the driver terminates it (ADR-0024
already requires terminate, kill, and reap on every driver exit path). This needs no protocol change and
no ADR edit.

| Case | On the wire | HAL returns | Exit |
|---|---|---|---|
| Normal completion, `close(kNormal)` | `CLOSED`, `CLOSE_SEQ`, wake | `ASTRA_HAL_CLOSED` | 0 |
| Any sim halt, `close(kAfterFailure)` | nothing; unmap, unlink | `ASTRA_HAL_TIMEOUT` at `peer_timeout_ms` (or killed by the driver) | 2 |
| Sim process killed | nothing | `ASTRA_HAL_TIMEOUT` | 2 |
| Sim idle longer than `peer_timeout_ms` (R4b) | nothing | `ASTRA_HAL_TIMEOUT` | 2 |
| Sim `open()` fails (e.g. handshake timeout) | nothing; unmap, unlink | `ASTRA_HAL_HANDSHAKE_TIMEOUT` if not yet attached, else `ASTRA_HAL_TIMEOUT` | 3 or 2 |
| No segment, never sized, or never `SIM_READY` within the handshake budget | none | `ASTRA_HAL_HANDSHAKE_TIMEOUT` | 3 |
| Bad magic, layout version, layout size, or ICD version | none | `ASTRA_HAL_BAD_LAYOUT`, immediately | 4 |
| Stale segment (`req_seq != SEQ_INIT` at attach), `CLOSED` seen during the handshake, or a request whose magic, version, or 64-bit frame disagrees with `req_seq` | none | `ASTRA_HAL_PROTOCOL` | 5 |
| System error, `hal_send` with nothing pending, `hal_wait_frame` with a reply pending, bad arguments | none | `ASTRA_HAL_ERROR` | 6 |

Exit 1 stays free for the program's own argument errors. `CLOSED` seen during the handshake is exit 5 (Joe's
change 1): in a correct run the sim closes only after it has seen `PEER_READY`, so a close before that point
is a protocol fault, not a clean end.

A sim-layout **smaller** than the peer's (an older version) looks like "not finished" under ADR-0024's size
rule, so it reports 3, not 4. This is disclosed, not claimed detectable.

**Alternatives:**
- **(A) Same wire as `kNormal`.** The peer exits 0 promptly after any failure. It is rejected because exit
  0 would then mean "the sim ended", not "the sim completed", which stretches ADR-0024's "on normal
  completion" and loses the independent witness.
- **(C) A fourth `sim_state` value `FAILED`, with `CLOSE_SEQ` and a wake.** Prompt, with a distinct status.
  It needs a superseding amendment to ADR-0024's enumerated `sim_state` values, which is Joe's to
  authorize.

### Q-4: How ADR-0024's two interleaving hooks stay test-only

**Recommend: no hook exists in any shipped object.**

- **Test 1 (creator paused before `ftruncate`, and after sizing but before `SIM_READY`) needs no product
  hook.** The test plays the creator on the layout header, and the real HAL attaches from another thread.
  Barriers, not sleeps, prove that the HAL actually ran inside each window (critique F-4). The test
  executable also wraps `fstat` and `mmap` (`-Wl,--wrap=fstat,--wrap=mmap`; the wrappers record calls on
  the segment's fd and pass everything else through). The sequence:
  Every barrier wait has a timeout (2 s), and a timed-out barrier fails the test naming the barrier, never a
  hang (Joe's change 3).
  1. `shm_open(O_EXCL)`.
  2. Block until the `fstat` wrapper has recorded the HAL seeing a size below 328.
  3. `ftruncate`.
  4. Block until the `wait_until` wrapper has recorded the HAL waiting on `&sim_state` (mapped, not yet
     ready). Assert `hal_init` has not returned and `peer_state` is still `NONE`.
  5. Header init, then release `SIM_READY`.
  6. Assert `hal_init` returns `OK`; that the `fstat` wrapper fired on the segment at least twice (once short,
     once full size) and the `mmap` wrapper fired on it exactly once (Joe's change 3), so the wraps provably
     took effect; and that no `mmap` of the segment happened while the last recorded size was below 328.

  The zero header (magic 0) during step 4 also means a premature validation would report `BAD_LAYOUT`.
- **Test 2 (close between the HAL's pre-wait check and its `FUTEX_WAIT`) uses a link-time wrap.** The test
  executable links `astra_hal` with `-Wl,--wrap=astra_hal_shm_wait_until`. Its
  `__wrap_astra_hal_shm_wait_until` runs the **real** plugin's `close(kNormal)` (loaded through
  `PluginLoader`, with the handshake done against the in-process HAL) the first time it sees `word ==
  &req_seq`. It then calls `__real_...` with the HAL's stale `expected`. The kernel sees `CLOSE_SEQ`,
  returns `EAGAIN`, and the HAL must return `ASTRA_HAL_CLOSED` well before its deadline. The test asserts the
  hook fired exactly once.

  `--wrap` redirects only undefined cross-object references, which is why `astra_hal_shm_wait_until` lives
  in its own TU with external linkage (documented there). The shipped `astra_hal` and echo peer are
  byte-for-byte the objects the test links. There is no macro, no global hook pointer, and no hook symbol.
  The same wrapper drives the deterministic precedence tests below (critique F-2, F-5).

**Alternative:** `#ifdef ASTRA_HAL_TEST_HOOKS` compiled into a test-only copy of the HAL. Simpler, but the
tested object then differs from the shipped one.

### Q-5: Config key for the segment name

**Recommend `transport.segment`:** a required string matching `astra_<1-10 digits>_<1-32 alphanumerics>`,
read by the shm plugin's `init`. It is required because a default name would collide across concurrent
runs. The pattern enforces ADR-0024's form, keeps `/` out, and lets the dead-creator fixture parse the pid.
The HAL gets the same string from `astra_hal_config.segment` (driver CLI).

**Alternative:** a nested `transport.shm: { segment }` block, parallel to the loopback's `transport.peer`.
Flat is recommended because there is one key and the plugin already owns every non-sim key in the block.

### Further decisions this plan makes (flag any you want reversed)

- **Q-6, unlink at handshake completion** (and again, idempotently, on close or failure). After `PEER_READY`
  nothing needs the name. A SIGKILLed sim then leaves no `/dev/shm` entry after the handshake, and a second
  stray peer can no longer attach and become a second writer of `ack_seq`. ADR-0024 says "the creator
  unlinks on close"; this unlinks earlier and still on close. Two peers racing inside the handshake window
  are not detected and **not claimed detectable**. One peer per unique name is the driver's contract.
- **Q-7, shm never returns `kPeerClosed`.** ADR-0024 gives the peer no closed state. A peer-side close
  signal would need a new rule on a waited word (the same lost-wake trap as F3). Peer exit or death is
  detected by the timeout (`kTimeout`), ADR-0024's "the timeout is the liveness detector". `hal_close`
  writes nothing. PR 3's `kPeerClosed` path stays covered by the loopback.
- **Q-8, timing tests run `RUN_SERIAL`.** Joe's allowance (500 ms timeout plus the greater of 20 ms and 10%,
  so 550 ms) is kept strict. The tests that assert it (and the stress test) live in separate executables
  with `RUN_SERIAL`, so `ctest --parallel` load does not add scheduling latency. This adds a few seconds of
  wall time per preset. Widening the allowance instead would weaken acceptance, so it is not proposed.

## Mapping and sequence

One Claude builder, one worktree, no packages. Everything new lives in `embedded/hal/`,
`plugins/sim/transport/shm/`, `tests/lockstep/`, one line in `CMakeLists.txt`, one `add_subdirectory` in
`tests/CMakeLists.txt`, and one test plus a compile definition in `tests/physics/`.

| Req | Change / owned paths | Contract | Check (all names match A4's regex) |
|---|---|---|---|
| Layout | `embedded/hal/astra_shm_layout.h`; `tests/lockstep/shm_layout_c11.c` | ADR-0024 header, static asserts | `HalShm_layout_c11_{gcc,clang}` (compile as C11 with `-Wpedantic -Werror`, also `astra_hal.h`); the C++17 and C++23 builds |
| HAL | `embedded/hal/{CMakeLists.txt,astra_hal.h}`, `embedded/hal/shm/{hal_shm.cpp,hal_shm_wait.cpp,hal_shm_internal.h}`; top-level `CMakeLists.txt` one line | R10, handshake, close precedence, exit table | `HalShm.*` below |
| Plugin | `plugins/sim/transport/shm/{CMakeLists.txt,shm.cpp,shm_wait.cpp,shm_transport.hpp}` | `Transport` contract, Q-2, Q-3, Q-5, Q-6, Q-7 | `ShmTransport.*`; `AllPlugins` (existing) |
| Peer | `tests/lockstep/{CMakeLists.txt,echo_peer.cpp,shm_test_support.{hpp,cpp}}` | echo/hold modes, fault flags | `EchoPeer.*`, `EchoPeer_links_no_astra_core` |
| R6 | `tests/physics/no_alloc_test.cpp` + `CMakeLists.txt` define | zero allocations with shm | `physics.NoAlloc.ZeroHeapAllocationsOverOneThousandFramesWithShmEchoPeer` |
| Stress | `tests/lockstep/echo_peer_stress_test.cpp`, **registered only when `CMAKE_BUILD_TYPE` is `Release`** (the release preset; asan, debug, and static-plugins never see it, which matters because A4's asan pattern `EchoPeer` also matches `EchoPeerStress`; Joe's change 2), label `stress`, `RUN_SERIAL`, timeout 600 s | 1e6 exchanges, integrity, cost | `EchoPeerStress.MillionExchangesEchoPayloadAndReportCost` |

`shm_test_support` provides a unique segment name (`astra_<getpid()>_<counter>`), a raw region mapper for
test-side creators and peers (built on the layout header), a child-process guard (posix_spawn, then SIGTERM,
a 1 s bounded wait, SIGKILL, and `waitpid` on every path), and the dead-creator sweep. The sweep unlinks
`/dev/shm/astra_<pid>_*` whose pid is gone; ADR-0024 allows exactly this fixture. Every lockstep fixture's
`TearDown` asserts that the child was reaped and that no `astra_<own pid>_*` segment remains ("no orphans").

**Tests** (executables: `astra_lockstep_tests` label `lockstep`, `astra_lockstep_timing_tests` label
`lockstep` with `RUN_SERIAL`, and the stress executable. The timeout is 500 ms unless stated. "Bound"
means `[timeout, timeout + max(20 ms, 10%)] = [500, 550] ms`, measured test-side with `steady_clock`,
where test code may read a clock):

- **R4, ShmTransport (plugin driven directly as core drives it, with a raw peer thread):**
  - `RepeatedWakeWithoutStoreDoesNotRestartDeadline` (timing): the peer calls `FUTEX_WAKE` on `ack_seq`
    every 2 ms without storing. Expect at least 100 `{kObserved, unchanged}` returns (reported, not
    classified), then `kExpired` with the wait inside the bound.
  - `SignalsDuringWaitDoNotRestartDeadline` (timing): a `SIGUSR1` storm to the waiting thread (handler
    without `SA_RESTART`) against a **silent** peer. With nothing else to wake the waiter, every
    `{kObserved, unchanged}` return is an interrupted wait. Require at least 10, then `kExpired` inside the
    bound (critique F-5).
  - `AckObservedAfterDeadlineIsExpiry` (deterministic, critique F-2), two cases:
    - A peer wake without a store returns the first wait early and arms the deadline. The test thread then
      sleeps past the deadline while the peer stores the correct ack before it. The next `wait_ack` returns
      `kExpired`, because the change was first observed after the deadline.
    - The peer stores the ack after the deadline, and the next call returns `kExpired`.

    Through `Simulation`, a late ack halts `kTimeout`.
  - `ExpiredDeadlineStaysExpiredUnderContinuousWakes` (deterministic, critique F-1): after the deadline, a
    peer thread issues `FUTEX_WAKE` in a tight loop. Every further `wait_ack` returns `kExpired`, and core's
    loop (`Simulation::step`) halts.
  - `OpenFailureReleasesDescriptorAndName` (critique F-3): an `ftruncate` failure is injected with
    `RLIMIT_FSIZE` below 328 (`SIGXFSZ` ignored, the limit restored afterward). `--wrap` cannot reach the
    plugin, because it is a dlopen'd object, or part of `libastra_core.so` under static-plugins. Expect a
    `ConfigError` at `transport`, no descriptor for the segment left in `/proc/self/fd`, no segment, and
    success on a repeated attempt. A handshake timeout (no peer) checks the same post-`mmap` path, with no
    mapping of the name left in `/proc/self/maps`. An `mmap` failure has no injection route here: it
    shares the same scoped owners, and that is a disclosed, review-only case.
  - `SilentPeerWaitDurationIsTheTimeout` (timing).
  - `EachPublicationGetsItsOwnDeadline`: acks delayed by 300 ms over three publications at timeout 500 never
    expire. A deadline carried over from the previous publication would expire at the second.
  - `UnchangedAckIsReportedNotClassified`.
  - `OutOfOrderAckIsReportedVerbatim` (and through `Simulation`, halts `kOutOfOrderAck`).
- **R4, EchoPeer (real `Simulation` with the real echo peer):**
  - `KillToHaltWithinOneTimeout` (timing): `--stop-after 20`, notify, killer thread SIGKILL. Expect
    `HaltReason{kTransport}`, `transport_fault() == kTimeout`, kill-to-halt at most 550 ms, clock frame
    equal to the halt frame, finite state, and no command staged after the halt.
  - `AbortToHaltWithinOneTimeout` (timing): `--abort-after`, measured from the notify byte (the last
    reply).
  - `SimPauseLongerThanPeerTimeoutEndsPeerNonzeroAndHaltsNextExchange` (timing; R4b on shm, peer timeout
    700): the peer exits 2 and the next `run()` halts `kTimeout`.
- **R5 on shm (EchoPeer):**
  - `PeerStartedFirstCompletesHandshake` and `SimStartedFirstCompletesHandshake` (the peer is spawned 300
    ms into `from_config`).
  - `NormalCompletionExitsPeerZeroPromptly`: the peer exits 0 less than 1 s after `Simulation` is destroyed,
    with `peer_timeout_ms` 5000, so the exit came from the close, not the deadline. Its summary reports
    every exchange.
  - `FailureCloseDoesNotReportCleanExit`: a limit violation halts `kCommandLimit`; the peer exits 2 at about
    its `peer_timeout_ms` (800), under Q-3 (B).
  - `StaleSegmentIsAnInitErrorAndIsLeftAlone`: a pre-created name gives a `ConfigError` at `transport`
    naming the segment. The segment still exists afterward and the fixture removes it.
  - `HandshakeTimeoutWithNoPeerLeavesNoSegment` (timing: the `ConfigError` arrives within
    `handshake_timeout_ms` plus the allowance).
  - `SegmentNameMustMatchPattern`: a missing, malformed, or over-long name gives a `ConfigError` at
    `transport.segment`.
  - `SimulationCompletesTenThousandExchanges` (divisor 1, hold mode): exchanges 10000, missed 0, every
    recorded reply echoes its request frame, and the peer's summary independently reports 10000.
  - `LinksNoAstraCore`, as `EchoPeer_links_no_astra_core`.
- **R10, HalShm (real HAL against a test-side creator, or the real plugin where stated):**
  - `NowIsFrameDerived`: 0.0 before any frame, then exactly the request's `t`.
  - `SendWithoutPendingFrameIsError`.
  - `WaitReportsTimeoutAtPeerDeadline` (timing).
  - `RepeatedWakeWithoutStoreDoesNotRestartPeerDeadline` (timing).
  - `CloseBeatsExpiry` (deterministic, critique F-5): the `wait_until` wrapper calls `__real_`, then, on an
    expiry result, runs the real plugin's `close(kNormal)` before returning. The HAL's post-expiry loads
    therefore see the close. Expect `CLOSED`, never `TIMEOUT`.
  - `RequestAfterExpiryIsTimeout` (deterministic, critique F-2): the same wrapper publishes an ordinary
    request instead. Expect `TIMEOUT`.
  - `OrdinaryExpiryIsTimeout`: the plain case, with no store.
  - `RequestFrameMismatchIsProtocolError`.
  - `StaleSegmentWithPublishedRequestIsProtocolError`.
  - `BadMagicIsImmediateBadLayout`, `BadLayoutVersionIsImmediateBadLayout`,
    `BadLayoutSizeIsImmediateBadLayout`, and `BadIcdVersionIsImmediateBadLayout`: each returns well before
    the handshake deadline.
  - `NoSegmentIsHandshakeTimeout` (timing).
  - `ExitStatusTable`: the Q-3 mapping.
  - `InitCloseInitCycle`.
  - `WaitAndSendAllocateNothing`: `tests/support/alloc_counter.cpp` linked into this executable, 1000
    frames against a non-allocating raw sim thread.
- **ADR-0024's interleaving tests (HalShm):**
  - `CreatorPausedBeforeSizingAndBeforeReadyAttachesCleanly`, per Q-4 test 1.
  - `CloseBetweenPreWaitCheckAndFutexWaitExitsPromptly`, per Q-4 test 2: less than 100 ms against a
    `peer_timeout_ms` of 5000, and the hook fired once.

Order: layout header and its C11 test; HAL and the wait TU; plugin; test support and echo peer; tests;
NoAlloc; stress; fast gate (`./scripts/check-fast.sh`); A4; full gate.

## Validation and risks

**Acceptance (A4, from the worktree root):**
`ctest --preset asan -R '(ShmTransport|HalShm|EchoPeer)' --no-tests=error`, then
`ctest --preset release -R EchoPeerStress --no-tests=error` (its output carries the measured per-exchange
cost: mean, p50, p99, max), then `./scripts/check-full.sh` with the log kept at
`docs/reviews/evidence/<sha>-full-gate.log`, and the A4 logs beside it. PR 3's suites (`SimulationTransport`,
`LoopbackTransport`, `NoAlloc`) and the M3 regression pass with no edits to their files
(`git diff --stat origin/main -- tests/unit tests/golden tests/configs` shows no changes to existing
files). Then one Codex adversarial review (memory ordering, lost wakes, deadline handling, close/expiry
precedence, cleanup and orphans, allocation on the per-exchange path) and the `/human-review` packet. Stop
before push.

**Accepted gap (M4 Q14).** ASan cannot see futex memory-order bugs, and TSan cannot follow cross-process
futex synchronization. The mitigations are the stress test's payload-integrity check in both directions
(FNV of the request, and fields echoed back), the ordering table above, and Codex's review.

**Risks.**
1. Timing tests under a loaded machine. They run `RUN_SERIAL`; if one still flakes, I report it and do not
   widen the bound (Q-8).
2. LeakSanitizer under ptrace can stall the asan phase. If it does, Joe runs the unchanged gate in a normal
   terminal and I report it; it does not count as a pass.
3. clang-tidy on the HAL and echo peer TUs (C-style names, `syscall` varargs, `reinterpret_cast` of the
   mapping). I fix these in code or with narrowly justified `NOLINT`, never by weakening `.clang-tidy`.
4. `--wrap` depends on the wait function staying in its own TU. If it is merged, the hook test fails loudly
   (it asserts the hook ran), not vacuously.
5. `static-plugins` links the shm plugin into `libastra_core`. That is harmless but gives core a futex
   syscall reference in that preset.
6. The stress test's cost is machine-specific. It is reported, not gated.

**Rollback:** revert the PR. Nothing outside a config naming `plugins/sim/transport/shm` changes behavior.
No new dependency (futex through `syscall`, `shm_open` in glibc), no golden, tolerance, ADR, or
architecture edit.

## Plan critique and decisions

One fresh, read-only Codex critique: `codex-companion.mjs task --fresh --prompt-file ...` (companion 1.0.6,
no `--write`; exit 0; model `gpt-6-astra` at reasoning effort `low`, as recorded in the thread's rollout;
thread `01a0dc4e-a43c-7fc1-bb94-9890b12ad0a3`). The focus was futex ordering: lost
wakes, acquire/release pairing, the single-writer rule, deadline re-waits, and close-versus-expiry
precedence. Raw output stays in the session scratchpad and is not retained in the repository, as for the
M4 and PR 3 critiques. Seven findings, none rejected. I checked each against the repository or the
kernel's behavior before accepting it.

Codex found **no defect** in:
- lost wakes on any of the four waits or the close sequence;
- acquire/release pairing;
- the single-writer rule under the one-peer contract (late `hal_send`, sim unmap/unlink, early unlink);
- the handshake order;
- Q-3 (B) against ADR-0024;
- the `--wrap` close hook;
- ADR-0023's two sites, or scope.

| ID | Sev | Verdict | Evidence and resolution |
|---|---|---|---|
| F-1 an absolute futex timeout alone does not bound core's loop | Major | Accepted | Linux `__futex_wait` returns `EAGAIN` on a value mismatch and 0 when a concurrent `FUTEX_WAKE` dequeues the waiter, both ahead of `ETIMEDOUT`. `lockstep_exchange.cpp:244` retries `{kObserved, unchanged}` forever. The helper now latches expiry by an explicit clock comparison after every return (the same function, ADR-0023's "time remaining"). New test: continuous wakes after the deadline. |
| F-2 a late ack or request overrode expiry | Major | Accepted | `transport.hpp:81-82`: "a call after that deadline returns kExpired"; M4 R5 lists "a late ack" as a halt; ADR-0024 gives precedence over expiry to close only. My draft returned a changed ack after expiry as `kObserved` and let the HAL accept a request after expiry. Now: load, then clock; an observation at or after the deadline is expiry; `kFault` is terminal; the HAL checks close first, then times out. Deterministic tests on both sides. |
| F-3 failed `open()` leaked the descriptor | Moderate | Accepted | The draft closed the fd only after a successful `mmap`, and core never calls `close()` after a throwing `open()` (`lockstep_exchange.cpp:158-170`). Three separately owned resources; injected-failure test with an fd scan and a repeat. |
| F-4 the paused-creator test could pass without the HAL in either window | Moderate | Accepted | Sleep-based pauses prove nothing about the HAL's schedule; mapping a zero-length object need not fault until touched. Barrier-driven sequence using test-side `--wrap` of `fstat`, `mmap`, and `wait_until` observations; assertions on no premature map or readiness. |
| F-5 the EINTR and close-at-expiry tests lacked decisive interleavings | Moderate | Accepted | A storm can land between syscalls. Silent peer, so every unchanged return counts as an interrupted wait (at least 10 required). Close and request precedence are forced through the wrapper after a real expiry result. |
| F-6 the stress integrity check used constant data | Moderate | Accepted | Constant `phi/theta/psi/p` would let a stale reply pass. Frame-dependent patterns in every variable field; full expected reply built per exchange and byte-compared. |
| F-7 the header as drawn had 4-byte alignment | Minor | Accepted | Checked with `gcc -std=c11`: a struct of only 32-bit members has `_Alignof` 4. `reserved` is now `uint64_t[3]`, still 64 bytes, and both structs assert 8. |

**Joe's decision (2026-09-25): approved**, Q-1 to Q-8 as recommended, with three changes and four nits,
all folded into the text above:

1. `CLOSED` seen during the HAL handshake returns `ASTRA_HAL_PROTOCOL` (exit 5), not 0. It is unreachable in
   a correct run, and step 4 checks it before validation.
2. CMake registers `EchoPeerStress` only in the release preset (`CMAKE_BUILD_TYPE` `Release`), because A4's
   asan pattern `EchoPeer` also matches it.
3. In the paused-creator test every barrier has a timeout, and the test asserts that the `fstat` and `mmap`
   wrappers fired.

Nits:
- compare the full 64-bit `request.frame` with `req_seq`;
- `hal_wait_frame` with a reply pending returns `ASTRA_HAL_ERROR`;
- the Codex model is recorded above;
- the duplicate decisions line is removed.

New tests for the changes: `HalShm.ClosedDuringHandshakeIsProtocolError`,
`HalShm.WaitWithReplyPendingIsError`, `HalShm.RequestFrameHighBitsMismatchIsProtocolError` (a request
whose `frame` agrees with `req_seq` in its low 32 bits only).

## Implementation record (2026-09-25/26)

Candidate `95369fe`, which is `937868c` plus two follow-up commits, all on `worktree-m4-pr4-shm-transport` off `7a349ee`. The human-review
packet is `docs/reviews/m4-pr4-shm-transport-human.md`. Evidence is under `docs/reviews/evidence/` with the
`95369fe-`, `d020d57-`, and `937868c-` prefixes.

**Departures from the test list above, all additive or renames within A4's selector:**
- `SegmentNameMustMatchPattern` and `StaleSegmentIsAnInitErrorAndIsLeftAlone` are in suite
  `ShmTransport`, not `EchoPeer`. They need no peer process.
- The handshake-timeout bound is measured on the plugin's `open()` directly, as
  `ShmTransport.HandshakeTimeoutWithNoPeerIsTheBudgetAndLeavesNoSegment`. Measuring around
  `from_config` would add init overhead to the bound. The same test also checks the `ConfigError`
  through `Simulation`.
- `ShmTransport.WaitUntilLatchesExpiryWhenTheKernelReturnsEarly` and
  `HalShm.WaitUntilLatchesExpiryWhenTheKernelReturnsEarly` were added after a mutation check. The
  continuous-wake tests alone could not force F-1's race: they pass with the clock latch removed, as
  the critique predicted. The new tests call each wait function directly with an already-past
  deadline, so the kernel returns `EAGAIN` and only the clock comparison can report expiry. To make
  this possible, `astra_lockstep_tests` compiles `plugins/sim/transport/shm/shm_wait.cpp` in; it is
  the same source, not a copy.
- The adversarial review (below) added three HAL handshake regressions.
- `ShmTransport.OutOfOrderAckHaltsThroughCore`, `BadReplyMagicAndStaleReplyFrameHaltThroughCore`,
  `LateAckHaltsThroughCoreAsTimeout`, and `ContinuousWakesHaltThroughCoreAsTimeout` drive `Simulation`
  with a raw scripted peer, so core's classification of the plugin's verbatim reports is exercised on
  shm.

**Mutation checks.** Each mutation was applied to one protection, built, the targeted tests run, and
the file restored; logs are in the session scratchpad. Every mutation made its targeted tests fail:

| Mutation | Test that fails |
|---|---|
| M1: `close()` without `CLOSE_SEQ` | `HalShm.CloseBetweenPreWaitCheckAndFutexWaitExitsPromptly` (5000 ms instead of prompt) |
| M2: plugin expiry by `ETIMEDOUT` only | `ShmTransport.AckObservedAfterDeadlineIsExpiry` and `WaitUntilLatchesExpiryWhenTheKernelReturnsEarly` |
| M2h: the same in the HAL | `HalShm.WaitUntilLatchesExpiryWhenTheKernelReturnsEarly` |
| M3: HAL accepts a request after expiry | `HalShm.RequestAfterExpiryIsTimeout` |
| M4: HAL maps a short segment | `HalShm.CreatorPausedBeforeSizingAndBeforeReadyAttachesCleanly` (SIGBUS under ASan) |
| Pre-fix `d020d57` HAL | all three round-1 regressions |

**Failed integration 1/2.** The full gate on `937868c` failed at clang-tidy on
`hal_shm_test.cpp:262` (`modernize-avoid-c-arrays`): clang-format had moved a NOLINT comment off
its line. All four presets' ctest had passed before it. The fix is `d020d57` (`std::array`), and
the summary is in `937868c-full-gate-failure-summary.log`.

**Codex adversarial review of `d020d57`** (against `7a349ee`; companion 1.0.6, `adversarial-review
--base origin/main`, exit 0, thread `01a0dc8d-d5a9-76f1-8467-736a77a71a36`; the model is not recorded
by the companion for review jobs). Verdict: needs-attention, two findings, both accepted. Codex
reported no further defect in memory ordering, lost wakes, exchange deadlines, exchange
close-versus-expiry precedence, production cleanup, per-exchange allocation or exceptions, or the
single-writer and clock-site discipline.

| ID | Sev | Verdict | Evidence and resolution |
|---|---|---|---|
| R-1 handshake readiness accepted from an unchecked reload after the deadline | Medium | Accepted, fixed in `95369fe` | In `hal_shm.cpp:108-115` (HAL) and `shm.cpp:144-145` (plugin), each loop reloaded the state word outside the checked wait. A HAL descheduled after an on-time return, or one that attached after its last on-time poll, would accept READY after the handshake budget. This is the handshake form of F-2. Both loops now decide on the wait's own value, loaded before its clock read, with expected INIT or NONE: an already-moved word returns `EAGAIN` at once and is still judged against the deadline. The deadline still arms at the first wait; everything before it is non-blocking syscalls, and ADR-0023 has the deadline computed "when the wait begins". Tests: `HalShm.ReadyFirstObservedAfterHandshakeDeadlineIsTimeout` and `HalShm.ReadyAfterLateAttachIsTimeout`. The plugin side has **no deterministic regression**: its wait is inside a dlopen'd object that `--wrap` cannot reach. It is covered by construction (the same checked-value rule) and by the wait-function unit test. |
| R-2 CLOSED at handshake expiry classified as `HANDSHAKE_TIMEOUT` | Medium | Accepted, fixed in `95369fe` | The expiry branch returned without rechecking `sim_state`, contrary to Joe's change 1. It now acquire-checks CLOSED on every non-returned result first (PROTOCOL, exit 5). Test: `HalShm.ClosedAtHandshakeExpiryIsProtocolError`. |

**Correction round 1/2.** Codex verified `d020d57..95369fe` (`adversarial-review --base d020d57`,
exit 0, thread `01a0dc9f-1e2a-75a3-8fbb-d3f6f8c483d4`). Verdict: approve. R-1 and R-2 are fixed, with
no new defect; from source tracing, each of the three regressions would fail against `d020d57`. I
also ran all three against `d020d57`'s HAL: all three fail there and pass on `95369fe`.

**Measured exchange cost** (Release, `EchoPeerStress`, one machine, per exchange from publish until
the reply is copied): mean 2.86 us, p50 2.73 us, p99 4.53 us, max 134 us over 1e6 exchanges in 3.0 s
on `95369fe`. Earlier runs gave a max of 1.9 to 2.1 ms (a scheduling outlier), with the same mean. The M4 plan's
unmeasured estimate was 20 to 100 us, so a 100k-frame lockstep run's exchange cost is about 30 ms,
not 0.2 to 1 s.

## Done when

A4 passes on the final candidate with retained evidence. PR 3's tests and the M3 regression pass
unmodified. One Codex adversarial review is dispositioned under the bounded fix loop. The `/human-review`
packet is written. Stop before push. Implementation completion is not Joe's approval, merge, or push.
