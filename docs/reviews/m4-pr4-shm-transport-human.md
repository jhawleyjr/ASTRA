# Human review packet: M4 PR 4, shm transport and HAL shm backend

PR / candidate base and head: base `7a349ee` (`origin/main`, #54's merge; re-fetched 2026-09-26 and unmoved).
Product candidate `95369fe` on branch `worktree-m4-pr4-shm-transport`, three commits:
- `937868c` implementation;
- `d020d57` a clang-tidy fix;
- `95369fe` correction round 1, Codex R-1 and R-2.

The tree is clean at `95369fe` apart from untracked evidence logs. The commit that adds this packet, the
evidence, and the plan's implementation record (`f073be9`) contains documents only, as does the commit
recording Joe's decision. Pushed on Joe's authorization (2026-09-26) and opened as a draft PR whose body is this
packet.

Spec/plan/task: [PR 4 plan](../plans/m4-pr4-shm-transport.md), approved by Joe on 2026-09-25 with Q-1 to Q-8
as recommended plus three changes and four nits. It sits under the [M4 plan](../plans/m4-lockstep.md) PR 4 row
(R4, R5, R10, A4) and follows ADR-0007 as amended by ADR-0024, ADR-0022, and ADR-0023.

Risk: Tier 3 (cross-process concurrency, futex memory ordering, a transport, health/halt behavior).

Decision requested: approve behavior for this PR and its squash-merge. **Approved by Joe on 2026-09-26** (see the
Decision record); the push and draft PR are authorized. No release requested.

## What changes and why

**Before:** the only `Transport` was the in-process loopback; no `embedded/` existed.

**After:**
- `transport.plugin: plugins/sim/transport/shm` with `transport.segment: astra_<pid>_<nonce>` runs the lockstep
  exchange against a separate process built only from `embedded/hal` and `icd/`.
- The two sides share one region layout, `embedded/hal/astra_shm_layout.h`: a 64-byte header, then the 200-byte
  request, then the 64-byte reply, 328 bytes in all, 8-byte aligned. The header has its own layout version.
- Waits are process-shared futex waits against one absolute `CLOCK_MONOTONIC` deadline.
- Configs without shm are untouched. No `core/` file changed; `kCoreApiVersion` is unchanged.

**End-to-end path of one exchange**, in reading order:
1. **Sim, `open()`** (`plugins/sim/transport/shm/shm.cpp`):
   - Creates the segment with `O_EXCL`; `EEXIST` means a stale segment, which is left alone.
   - Sizes it, maps it, and writes the whole header.
   - Release-stores `SIM_READY`, then waits for `PEER_READY` against the handshake budget.
   - Checks that the ack word is still the sentinel, then unlinks the name (Q-6).
   - Any failure after `shm_open` releases the descriptor, the mapping, and the owned name, because core never
     calls `close()` after a failed `open()`.
2. **HAL, `hal_init`** (`embedded/hal/shm/hal_shm.cpp`), under one handshake deadline:
   - Retries `shm_open`, never maps a segment smaller than 328 bytes, then maps it.
   - Waits for `SIM_READY`. CLOSED at any point in the handshake is PROTOCOL.
   - Only then validates magic, layout version, size, and ICD version. A `req_seq` other than the sentinel
     means a stale session.
   - Release-stores `PEER_READY`.
3. **Per exchange**:
   - `publish`: payload `memcpy`, release `req_seq`, `FUTEX_WAKE`.
   - `hal_wait_frame`: acquire `req_seq`, `memcpy` out, check magic, version, and the 64-bit frame.
   - `hal_send`: payload `memcpy`, then release `ack_seq` set to the request's sequence (never `out->frame`), then
     wake.
   - `wait_ack`: returns the observed ack verbatim; core classifies it.
   - `read_reply`: copies all 64 bytes.
4. **The one clock function per side** (`shm_wait.cpp`, `hal_shm_wait.cpp`; ADR-0023's two sites):
   - It arms on its first wait and makes one `FUTEX_WAIT_BITSET`.
   - It then loads the word and **then** reads the clock. An observation at or after the deadline is expiry,
     and expiry is latched (critique F-1, F-2).
   - The HAL outranks expiry only with a close; the sim reports a late ack as `kExpired`.
5. **`close()`**:
   - `kNormal` stores `CLOSED`, then `CLOSE_SEQ`, then wakes both words; the HAL exits 0.
   - `kAfterFailure` writes nothing; the peer times out and exits 2 (Q-3).
   - `hal_close` writes nothing.

**Your decisions as implemented:**

| Decision | As built |
|---|---|
| Q-1 | One top-level line, `add_subdirectory(embedded/hal)`, with the in-tree C++17 static library `astra_hal`. No `embedded/CMakeLists.txt`. |
| Q-2 | The deadline is fixed at the first `wait_ack` after `publish`. |
| Q-3 | Exit statuses: 0 closed, 2 timeout, 3 handshake timeout, 4 bad layout, 5 protocol, 6 error. |
| Q-4 | The interleaving hooks are link-time `--wrap` in the test executables only. |
| Q-5 | `transport.segment`. |
| Q-6 | The name is unlinked at handshake completion. |
| Q-7 | The shm transport never reports peer-closed. |
| Q-8 | The timing tests are `RUN_SERIAL`. |
| Change 1 | CLOSED during the handshake is PROTOCOL (exit 5). |
| Change 2 | `EchoPeerStress` is registered only when `CMAKE_BUILD_TYPE` is Release, `tests/lockstep/CMakeLists.txt:65`. |
| Change 3 | Every barrier in the paused-creator test times out (2 s), and the test asserts that the `fstat` and `mmap` wrappers fired. |
| Nits | The 64-bit frame compare; `hal_wait_frame` with a reply pending is an error; the Codex model is recorded in the plan; the duplicate decisions line is removed. |

**Diff:** 28 files, +4483/-1 (`git diff --stat 7a349ee..95369fe`):
- product: about 1000 lines across the HAL (5 files) and the plugin (4 files);
- tests: about 2800 lines under `tests/lockstep/` (13 files), plus a NoAlloc test;
- three build lines;
- the plan.

`cmake/AstraPlugin.cmake` gains the three lockstep test executables in its explicit plugin-dependency list. No
golden, tolerance, ADR, architecture, or `icd/` file is touched.

## Evidence

All commands were run from the worktree root, at 2026-09-26, on the stated revision.

| Check / acceptance | Result | Log / candidate |
|---|---|---|
| A4: `ctest --preset asan -R '(ShmTransport\|HalShm\|EchoPeer)' --no-tests=error` | exit 0, 60 of 60 passed, 14.3 s | [95369fe-a4-asan.log](evidence/95369fe-a4-asan.log), `95369fe` (untracked evidence logs only) |
| A4: `ctest --preset release -R EchoPeerStress --no-tests=error` | exit 0; 1e6 exchanges in 3.03 s, mean 2.86 us, p50 2.73 us, p99 4.53 us, max 134 us | [95369fe-a4-stress.log](evidence/95369fe-a4-stress.log) |
| Full gate `./scripts/check-full.sh` | exit 0, `check: OK`. Four presets passed 510, 511 (release includes the stress test), 510, and 510 tests; clang-tidy, pyright (0 errors), and pytest (116 passed) all pass | [95369fe-full-gate.log](evidence/95369fe-full-gate.log) |
| Earlier candidate `d020d57`: full gate and A4 | exit 0, `check: OK`; 57 of 57; stress mean 2.78 us | `d020d57-*.log` (superseded by round 1) |
| `937868c` full gate | **failed** at clang-tidy (`hal_shm_test.cpp:262`, `modernize-avoid-c-arrays`) after all four presets passed. This is failed integration 1/2 | [937868c-full-gate-failure-summary.log](evidence/937868c-full-gate-failure-summary.log) |
| M3 frozen regression and PR 3's suites | pass unmodified inside every preset's run. `git diff 7a349ee..95369fe` touches no file under `tests/unit`, `tests/golden`, or `tests/configs` | full-gate log |
| Mutation checks | each targeted test fails when its protection is removed; table in the plan | plan, "Implementation record" |

LeakSanitizer did not stall in this session: the asan phase and A4 both completed. Toolchain is clang-18 and gcc
(C11 layout checks). The stress cost is from one machine and is reported, not gated. The M4 plan's unmeasured
estimate was 20 to 100 us per exchange; the measurement is about 3 us. No GitHub CI has run: nothing is pushed.

## Independent review

Builder: Claude (Opus 5.5). Reviewer: Codex through companion 1.0.6. The plan critique ran on `gpt-6-astra` at
`low` effort, as recorded in its rollout. The two review jobs did not record their model; see the evidence log
headers.

- **Plan critique** (fresh thread `01a0dc4e`): 7 findings (F-1 to F-7), all accepted before your approval. See the
  plan.
- **Adversarial review of `d020d57`** ([log](evidence/d020d57-codex-adversarial.log), thread `01a0dc8d`):
  needs-attention, 2 medium findings, both accepted and fixed in `95369fe`.
  - **R-1:** handshake readiness was accepted from an unchecked reload after the deadline, on both sides.
  - **R-2:** CLOSED at handshake expiry was reported as `HANDSHAKE_TIMEOUT` instead of PROTOCOL.

  Nothing else was found in memory ordering, lost wakes, exchange deadlines, exchange close precedence, cleanup,
  per-exchange allocation, or single-writer and clock-site discipline.
- **Correction round 1/2 verification** ([log](evidence/95369fe-codex-verify.log), thread `01a0dc9f`, diff
  `d020d57..95369fe`): approve; R-1 and R-2 fixed, no new defect. I also ran the three new regressions against
  `d020d57`'s HAL: all three fail there and pass on `95369fe`.
- Open findings: none. Rejected findings: none. Correction rounds 1/2; failed integrations 1/2.

## Joe's attention

**Read first:**
- `embedded/hal/shm/hal_shm_wait.cpp` and `plugins/sim/transport/shm/shm_wait.cpp`: the deadline algorithm. They
  are deliberately the same code; ADR-0024 makes the header an include, not a link.
- `embedded/hal/shm/hal_shm.cpp`: `complete_handshake`, and `hal_wait_frame`'s expiry branch.
- `plugins/sim/transport/shm/shm.cpp`: `open()`'s ownership and cleanup, and `close()`.
- `embedded/hal/astra_shm_layout.h`: the protocol comment and the ownership table.
- `tests/lockstep/wraps.cpp` plus `HalShm.CloseBetweenPreWaitCheckAndFutexWaitExitsPromptly` and
  `HalShm.CreatorPausedBeforeSizingAndBeforeReadyAttachesCleanly`: ADR-0024's two interleaving tests.

**Assumptions and limits:**
- ASan cannot see futex memory-order bugs and TSan cannot follow cross-process synchronization (accepted gap, M4
  Q14). The mitigation is the stress test's full-reply byte comparison against frame-varying data, plus review.
- The plugin side of R-1 has **no deterministic regression**: its wait is in a dlopen'd object `--wrap` cannot reach.
  It is covered by construction and by the wait-function unit test.
- An `mmap` failure in `open()` has no injection route; that path is review-only.
- Two peers racing inside the handshake window are not detected, and that is not claimed.
- A sim layout smaller than the peer's reports exit 3, not 4.

**Consequential choices beyond the plan's list:**
- `astra_lockstep_tests` compiles the plugin's `shm_wait.cpp` in (the same source), to unit-test the latch.
- The physics executable compiles `tests/lockstep/shm_test_support.cpp` for the NoAlloc shm test.
- The build/python (Release) tree also compiles the stress test but never runs ctest.

**Rollback:** revert the squash commit. Nothing outside a config naming the shm plugin changes behavior, and no
interface, API version, or dependency changed.

## Decision record

State: approved
Human decision/date/reference: Joe, 2026-09-26, in session: "Approved: candidate 95369fe (docs f073be9),
behavior as in the packet, with the unrecorded Codex model and low effort accepted as disclosed." He authorized
the push of `worktree-m4-pr4-shm-transport` and a draft PR with this packet as its body; not to be merged or
marked ready by Claude.
Approved candidate/scope: product `95369fe`; documents `f073be9` plus the commit recording this decision
(documents only). Behavior as described above. The unrecorded Codex review model, and the plan critique's
`low` reasoning effort, are accepted as disclosed.
Merge observed: none; Joe squash-merges
Release authorization: not requested

Significant tradeoff rationale: close after failure is silent (Q-3). CLOSED stays proof of normal completion; the
cost is that after a sim failure the peer lingers until `peer_timeout_ms`, or until the PR 6 driver kills it.
Accepted risks: the M4 Q14 memory-order detection gap (owner Joe; trigger: any flake in the stress test or the
timing tests). The unrecorded Codex review model (owner Claude; trigger: companion support for recording it).
Nonblocking follow-ups:
1. A wall-clock symbol scan that allowlists the two named clock functions (architecture §15, ADR-0023; not
   scheduled).
2. PR 5 makes `embedded/hal/CMakeLists.txt` standalone-capable and replaces the top-level line.
3. PR 6's driver uses `transport.segment` and the HAL exit-status table.
Architectural decisions: none new; implements ADR-0022, ADR-0023, ADR-0024.
