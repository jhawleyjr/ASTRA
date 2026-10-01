# M4 PR 1: ADR-0022/0023/0024 + architecture edits (Tier 3, policy)

Recovered from `~/.claude/plans/pasted-content-id-cf7e-m4-pr-buzzing-pie.md` on 2026-09-30. Approval: the file records none; the work merged as #51. Never committed: neither the file nor git history says why.

## Context
M4 Lockstep's spec and plan (`docs/plans/m4-lockstep.md`, merged as e16d26a, #50) are approved.
Before the transport code in PRs 2 to 6 can exist, its decisions need to be recorded: the exchange stage and its
protocol, the ADR-0007 shared-header amendment, and the monotonic-clock exception to ADR-0010. The
architecture doc also needs to match (including §11's count and §16's two turbulence levels). This PR
contains docs only. Joe's decisions from this session (they override my earlier assumptions):
- Item (b) becomes its own record, **ADR-0024** (it supersedes part of ADR-0007: the "mirrored byte-for-byte" consequence and
  the sequence/handshake protocol).
- **Two commits in one PR.** (1) The *review commit*: ADR-0022/23/24 with `Status: proposed` and Approval
  pending. ADR-0007 and ADR-0010 are **not touched**. (2) After Joe's read and his re-arming of the marker, a *flip commit*
  that is status and banners only: the three ADRs become `Status: **accepted**, <date>` with Approval filled in, and
  "**Superseded in part by ADR-00NN** (<date>): …" banners go under the Status line of ADR-0007 (→0024)
  and ADR-0010 (→0023, the transport-timeout clock exception only). Push only after the flip.
- AGENTS.md's wall-clock line gets one appended clause. Every other doc statement that says the wall clock is RealTime-only or that the HAL
  mirrors byte-for-byte is fixed the same way. Hits in hooks, skills, or scripts are listed for Joe, not edited. The commit
  body and STATE.md note these out-of-row edits.
- Plan doc: the PR 1 row names ADR-0024 for item (b), and the PR 4 row's "0007 (as amended)" becomes "0007, 0024".

## Step 0: housekeeping (main checkout)
1. `git worktree remove .claude/worktrees/m4-lockstep-plan` (it is clean). If it refuses, **stop**. No `--force`.
2. `git worktree prune`. Leave local branches.
3. `git fetch origin`. Confirm `git rev-parse origin/main` = e16d26a…, then
   `git worktree add .claude/worktrees/m4-pr1-adrs -b worktree-m4-pr1-adrs origin/main`. Do all later git
   work with `cd` in the worktree so the no-main hook sees the task branch.

## Step 1: unguarded edits (in the worktree)
**`docs/architecture.md`**
- Header status: append "rev 8, 2026-09-24: §2/§5/§10/§11/§15/§16 for ADR-0022/0023/0024 (proposed)".
  The flip commit changes this to "accepted by Joe <date>" (status only).
- §2 principle 1 (grep hit, out-of-row): "No model, transport, or embedded process reads the wall clock"
  gets the clause "for time; the lockstep transport's wait deadline (ADR-0023) is the one exception and never
  supplies a time value".
- §5: the per-frame order spells out the exchange stage. It runs for the bound vehicle (v1: index 0) at `frame % divisor == 0`, after the
  frame's environment sample and before `vehicle.step()`. It writes the config-declared command map through `InputRegistry`
  handles, and the same frame's `latch()` commits them, so there is zero compute delay (disclosed). The phrase "a `step()`-time writer (M4's transport/
  `ActuatorModel`)" is corrected. The lockstep bullet notes that the wait's timeout deadline is monotonic-clock (ADR-0023).
- §10: the `SharedMemoryTransport` bullet is refined per ADR-0024: one shared layout header in `embedded/hal/`,
  `UINT32_MAX` sentinels, readiness/CLOSED states, the receive state machine, and the single deadline. "Peer liveness is a pid" becomes
  "the timeout is the detector". The `Transport` interface sentence says the exact shape belongs to PR 3 under ADR-0022. Add
  the exchange recorder as M4's log (ADR-0022; superseded by M5's HDF5). Add the `throttle[2]` equality rule.
- §11: the HAL `shm` backend is "built on the shared layout header (ADR-0024)", not a "mirror image". Add
  wait-with-timeout, init/close, and CLOSED → exit 0. "10,000 lockstep frames" becomes "10,000 lockstep exchanges
  (100,000 major frames at the default divisor 10)" (Q1).
- §15: the handshake bullet becomes header-only readiness (magic, version, layout size) within `handshake_timeout_ms`,
  with no probe frame. The transport-health bullet becomes unconditionally terminal, with no `warn`/`off` action, `HealthCheck::kTransport`, and
  the failures enumerated, including sequence-range exhaustion, command-limit violation, and throttle mismatch. The determinism guard gains "except the transport
  wait deadline sites (ADR-0023), which the symbol scan must allowlist".
- §16 M4 row: 10,000 lockstep exchanges (100,000 frames), zero missed. Altitude ±30 m and heading ±2° at every
  major frame **at each of two gating Dryden levels at 10,000 m (Joe's MIL-F-8785C Fig. 7 read): light
  (σ 0.15 m/s, W20 15 kt) and moderate (σ 1.5 m/s, W20 30 kt)**. Bit-identical exchange-recorder output across two
  runs. Killing the autopilot produces a transport-health halt within one timeout (plus the wake allowance, Q3), not a hang.

**`AGENTS.md:47`** gets one appended clause: "… per ADR-0010 and the lockstep transport timeout (ADR-0023); it
never supplies simulation time". The line count stays the same (AGENTS.md + CLAUDE.md stay under 100 lines).

**`docs/plans/m4-lockstep.md`**: the PR 1 row's (b) names ADR-0024 and PR 4's "0007 (as amended)" becomes "0007, 0024". I also
propose PR 3's "new ADR" → "0022, 0023" (flagged to Joe as a small extension of his instruction).

**Grep done in planning** (AGENTS.md, CLAUDE.md, docs/, .claude/skills, .claude/hooks, scripts): the doc hits are
AGENTS.md:47, architecture.md:30/399/532 (all handled above) and the ADR-0007/0010 lines, which the flip-commit banners cover. There were **no hits in
hooks, skills, or scripts**. Plan-doc Q2/Q4 text is a decision record and is left alone. I'll re-run the grep before commit.

## Step 2: draft ADR text in the scratchpad (unguarded), then stop and ask
Write the three drafts to the scratchpad in the template shape (`docs/templates/adr.md`; header style of ADR-0019/0021),
**Status: proposed**, Approval: pending.
- **ADR-0022: Lockstep exchange stage owned by `Simulation`.** It elaborates 0006/0007/0009/0010/0011/0017 and
  resolves ADR-0017's Known gap for the transport by a third route: the core stage writes through `find()` handles resolved
  after every `bind_inputs()`, so it has no load-order dependency. `ActuatorModel` stays open. It records: stage placement, ZOH, and zero delay; the
  `transport` config block and the command map (config data, never in `core/`); unknown keys or names raise `ConfigError`; `throttle[2]`
  equality; per-command limits that halt with no clamp (Q9); a zero-initialized pack; the receive state machine (what is and is *not*
  detectable); `frame < UINT32_MAX` at every publication; unconditionally terminal failure
  (`HaltReason{kTransport, source, frame, vehicle_index}`, the clock does not advance, no action key); NaN goes to `health.input`;
  a handshake failure is an init error naming `transport`; timeouts 1000/5000 ms defaults, one immutable deadline, and the strict "within one
  timeout" reading (Q3); the exchange recorder (three streams plus final state, a fixed header with counts and record sizes,
  native-endian fixed records); `mode_flags`/`fault_flags` recorded, not acted on (Q16); no block means bit-identical;
  `kCoreApiVersion` 7→8. Rejected: a transport model writing through the seam (load order), ActuatorModel-as-WrenchSource,
  `warn`/`off`, clamping, and per-exchange-only state sampling.
- **ADR-0023: Monotonic-clock exception for the lockstep transport wait.** It supersedes in part ADR-0010's "no clock
  outside the RealTime pacer" consequence (and the Context sentence) for this purpose only. `CLOCK_MONOTONIC` via
  `clock_gettime` is allowed only to compute one absolute deadline per blocking wait and the remaining time on re-wait. The sites are the
  sim-side shm plugin (handshake and ack waits) **and the `embedded/hal` shm backend's waits** (the peer end of the same
  timeout, needed for R4b; `hal_now` stays frame-derived and the autopilot reads no clock). The value never enters
  state, signals, ICD, or the recorder; expiry is only a halt or a nonzero exit; the code is confined to one function per side so the §15 scan can allowlist it.
  Loopback reads no clock. Rejected: a relative-timeout retry loop (the bound drifts), no timeout (hangs), `CLOCK_REALTIME`, timerfd.
- **ADR-0024: Shared shm layout header and protocol (supersedes ADR-0007 in part).** Superseded: line 39's
  "mirrored byte-for-byte", the "mirror-image backend", zero-valued `req_seq = frame` at frame 0, and pid-based liveness. Still in force:
  shm + raw futex and its rejected alternatives, the divisor and ZOH, blocking in either mode, Loopback, the halt consequence, and the M4 acceptance.
  It records: one C11/C++17-compatible header in `embedded/hal/` (includes only `<stdint.h>` and `icd/astra_icd.h`), consumed by the shm plugin
  include-only (ADR-0008's link rule unchanged). The layout is static-asserted. `UINT32_MAX` sentinels. **The readiness state is split into two
  words, one per writer (`sim_state`: INIT→SIM_READY→CLOSED; `peer_state`: PEER_READY)**, so each word has a single writer.
  O_EXCL create; init then release-publish; the peer retries attach until the handshake timeout and validates with acquire; no payload before
  both are ready; header-only handshake, no probe frame. Payload is written before the release store of the sequence word, and the reader acquire-loads before reading. Process-shared
  futex; spurious wakeups re-wait against the same deadline. CLOSED gives a clean HAL exit 0; the creator unlinks. The name is `astra_<driver pid>_<nonce>`
  from config; the driver unlinks in `finally` with a bounded terminate/kill/reap; a fixture cleans dead-creator leftovers; there is no unlink-on-next-start;
  `sim_pid` is diagnostic only. Rejected: mirrored copies plus an equality test, zero-init words, a probe frame, unlink-on-start, pid probing,
  and one state word with two writers.

**Then stop and ask Joe to create `.claude/touch-goldens` in the main checkout.** I do not create it myself. Once he confirms,
Write the three files into `docs/decisions/` and tell him the marker can be removed. No edits to 0007/0010 in this commit.

## Step 3: A1, commit, review
1. `python3 scripts/docs_check.py && git diff --check` (with the new files staged/intent-added so they're covered). Re-run the grep.
2. STATE.md is rewritten (≤40 lines). In flight: PR 1 review commit, ADRs proposed. It notes the out-of-row edits
   (AGENTS.md:47, architecture §2 principle 1, the plan-doc rows), no hook/skill/script hits, counters, and next = Joe's read, then the flip.
3. Commit the review commit. The body lists the scope, the out-of-row edits, and "Codex review pending".
4. `/review-cycle`: one Codex **adversarial-review** `--base origin/main --wait`. The focus is set by a quoted heredoc:
   read `docs/plans/m4-lockstep.md` (decisions Q1–Q17, critique F-1..F-10), check ADR-0022/23/24 and the
   architecture/AGENTS edits against it and against accepted ADR-0006/0007/0008/0009/0010/0011/0017, look for internal
   contradictions, loosened decisions, and anything not stated or undetectable. Exit status is captured and raw output kept in the scratchpad. Verify each
   finding and fix the supported ones in a follow-up commit (ADR edits need the marker again; I'll ask). Counters start at 0/2. Re-run A1.
5. Show Joe the ADR list (`ls docs/decisions/`, plus titles and status of 0022–0024) and `git diff origin/main -- docs/architecture.md`
   limited to §16. Also report: the commands run with their exits, the Codex findings and verdicts, and the flagged refinements (the two-word state split, the HAL
   clock sites in ADR-0023, limit/throttle-mismatch as `kTransport` naming the ICD field, the PR 3 row edit).
   **Stop before push.**

## Step 4: after Joe's read (a later turn)
Once Joe re-arms the marker: the flip commit (status, Approval, and the 0007/0010 banners; the architecture rev line becomes "accepted"; the body says
"status/banner only"). Re-run A1. Push only then, and open a draft PR. Joe squash-merges.

## Verification
- `python3 scripts/docs_check.py` exits 0; `git diff --check` is clean; I read both exits.
- A Codex adversarial review completed (exit 0, non-empty output) over `origin/main..HEAD`; findings are recorded.
- `git diff origin/main --stat` shows only docs/architecture.md, docs/decisions/0022–0024 (new), AGENTS.md,
  docs/plans/m4-lockstep.md, and STATE.md. No tests/golden or tolerance files, and no edits to accepted ADRs in the review commit.
- The product gate is not run (Tier 3 docs/policy; no executable change). That is recorded as scope, not claimed as a pass.
