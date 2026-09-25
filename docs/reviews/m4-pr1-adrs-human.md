# Human review packet: M4 PR 1, lockstep decision records and architecture rev 8

PR / candidate base and head: base `origin/main` = `e16d26a` (still main's tip at packet time, fetched
2026-09-24; nothing to re-validate). Reviewed candidate head: `91107c4` on `worktree-m4-pr1-adrs` (two
commits: `d058a3a` review commit, `91107c4` correction round 1). This packet and two retained Codex logs are
added in a later docs-only commit; they change no ADR, architecture, or plan text. **Not pushed. No PR yet.**
Spec/plan/task: [docs/plans/m4-lockstep.md](../plans/m4-lockstep.md), PR 1 row (decisions Q1 to Q18).
Risk: Tier 3 (policy). Transport/concurrency and health-behavior decisions recorded as ADRs; docs only, no
executable, build, test, or golden change.
Decision requested: (1) accept ADR-0022, ADR-0023, ADR-0024 as written; (2) approve the out-of-row edits below;
(3) authorize the flip commit (status and banners only) and then push and a draft PR. Merge and release are
**not** requested here; Joe squash-merges.

## What changes and why

Before: M4's design decisions existed only in the plan. ADR-0007 left the exchange stage, command routing,
failure semantics, and the HAL layout open (or wrong: "mirrored byte-for-byte", zero-valued `req_seq = frame`);
ADR-0010 and `AGENTS.md` forbade any clock read outside `RealTime`; architecture §11 said "10,000 lockstep
frames" while §16 and ADR-0007 said exchanges. After: three proposed ADRs and matching architecture text let
PRs 2 to 6 implement against recorded decisions.

| Record | Scope | Supersedes / keeps |
|---|---|---|
| **ADR-0022** exchange stage | `Simulation` owns the stage (after the frame's environment sample, before `vehicle.step()`); config-declared command map written via `InputRegistry::find()` handles resolved after all `bind_inputs()`; ZOH and zero compute delay; receive state machine (unchanged `ack_seq` = still waiting; what is not detectable is stated); **unconditionally terminal** `kTransport` (no `warn`/`off`, no config key), clock does not advance; strict timeout reading; command validation order (frame/magic/version, finite, `throttle[2]` equality, limits; limits halt, no clamp); recorder as M4's log (three streams, fixed header); `kCoreApiVersion` 7 to 8 | Supersedes nothing. Resolves ADR-0017's disclosed M4 gap for the transport by a route it did not list; `ActuatorModel` stays open |
| **ADR-0023** clock exception | `CLOCK_MONOTONIC` only to compute one absolute deadline per blocking wait (and remaining time on re-wait); two allowed sites: sim shm wait, HAL shm wait; never enters state, signals, ICD, or recorder; expiry is only a halt or nonzero exit | Supersedes ADR-0010 **in part**: only the "no clock outside `RealTime`" consequence and the matching Context sentence. Frame counter as time, `t` derived, divisors, continuity: kept |
| **ADR-0024** shm layout + protocol | One layout header in `embedded/hal/` (includes only `<stdint.h>` + `icd/astra_icd.h`) used by plugin and HAL; two single-writer state words; `UINT32_MAX` sentinel and reserved `UINT32_MAX - 1` close value; header-only handshake; per-exchange acquire/release ordering; `CLOSED` shutdown; segment ownership and cleanup, no pid probing, no unlink-on-start | Supersedes ADR-0007 **in part**: "mirror-image backend", "mirrored byte-for-byte", literal `req_seq = frame`, pid liveness. Kept: shm + raw futex and its rejected alternatives, divisor/ZOH, blocking in either mode, Loopback, halt consequence, M4 acceptance. ADR-0008's link rule unchanged |

Architecture rev 8: §2 principle 1, §5 (per-frame order names the exchange stage; the "`step()`-time writer"
phrase corrected), §10 (Transport, shm refinements, `throttle[2]`, recorder), §11 (HAL wait/init/close, CLOSED
exit 0; **count corrected to "10,000 lockstep exchanges (100,000 major frames at divisor 10)"**), §15
(header-only handshake; terminal transport health with the failure list; determinism guard says **no symbol
scan exists today, and a future one must allowlist the ADR-0023 sites**), §16 M4 row (**both gating levels:
light 0.15 m/s / W20 15 kt, moderate 1.5 m/s / W20 30 kt**; every major frame; recorder bytes; kill-to-halt
within one timeout plus the Q3 allowance). Diff stat: 7 files, +313/-38 (three new ADRs = 267 lines).

**Out-of-row edits (your instruction), for approval:**
- `AGENTS.md` line 47: one appended clause, "and the lockstep transport timeout (ADR-0023)"; file still 68 lines.
- `docs/architecture.md` §2 principle 1: the same exception, in one clause.
- `docs/plans/m4-lockstep.md`: PR 1 row (b) names ADR-0024; PR 3 cites 0022, 0023; PR 4 cites "0007, 0024";
  R5, the contract text, and Q6 amended, and new **Q18** records the review round (below).
- Grep of `AGENTS.md`, `CLAUDE.md`, `docs/`, `.claude/skills/`, `.claude/hooks/`, `scripts/` for
  "RealTime-only wall clock" or "byte-for-byte HAL mirror" statements: the only remaining hits are inside
  ADR-0007 and ADR-0010, which the flip banners cover. **Nothing in hooks/skills/scripts to list.**
- ADR-0007 and ADR-0010 are **not edited** in the reviewed commits.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| A1 `python3 scripts/docs_check.py` | exit 0, "docs-check: OK (24 Markdown files checked)"; run on the tree before `d058a3a` and on the tree before `91107c4` | commands in this session; not retained as a file (one-line output) |
| A1 `git diff --check` | exit 0 at both points | same |
| Codex adversarial review of `e16d26a..d058a3a` (`--base origin/main`, exit 0) | verdict **needs-attention**, three findings | [log](evidence/d058a3a-m4-pr1-codex-adversarial-review.log) |
| Codex fix verification of `e16d26a..91107c4` (exit 0) | verdict **approve**, no material findings | [log](evidence/91107c4-m4-pr1-codex-fix-verification.log) |
| Product gate (`./scripts/check-full.sh`) | **not run**: your instruction (no product gate unless asked) and no executable, build, test, or runtime-policy change; recorded as scope, not as a pass | last full gate is M3's `f3c1460-full-gate.log`, unchanged |

Limitations: nothing here is tested code. Every behavior the ADRs specify (finite check order, close
sequence, handshake polling, deadline handling) is specification; PRs 3 and 4 implement and test it, and
PR 3 and PR 4 each get their own plan critique and adversarial review (plan, PR table). ASan/TSan cannot
validate futex ordering (plan Q14, accepted). CI is local-gate mode; nothing on GitHub has run. Codex
model identity is not recorded in the companion's output (companion 1.0.6, `adversarial-review`, read-only).

## Independent review

Builder: Claude (Opus 5.5 at the start of this work, Sonnet 5 for the later edits, per the session's
system notices). Reviewer: Codex via the installed companion, fresh context, read-only. **Round 1 of 2**
used; failed integrations 0/2.

| ID | Sev | Finding | Verified how | Resolution |
|---|---|---|---|---|
| F1 | high | ADR-0024 attach: `open` retry alone can attach to an unsized or unfinished segment; acquire-load without observing `SIM_READY` publishes nothing | Read the drafted handshake text: no size check, no wait-for-`SIM_READY` | Peer checks `fstat` size before mapping; acquire-polls until `SIM_READY` before any field; one handshake deadline; creator-pause tests. **Fixed** |
| F2 | high | ADR-0022: "non-finite command halts through `health.input`" is false when `sim.health.input` is `warn`/`off` | `core/src/kernel/health_monitor.cpp:151-154` applies `input_action_` (parsed from config, `health_monitor.hpp:222`, default halt) | **Your decision (alternative option):** exchange stage checks finiteness before limits and before `InputHandle::write()`, halts `kTransport` naming the ICD field; `sim.health.input` unconstrained; ADR-0017 stays a backstop. R5 and architecture §15 changed to match. **Fixed** |
| F3 | medium | Lost wakeup: HAL waits on `req_seq` but `CLOSED` is stored in `sim_state`; normal completion could wait out the deadline and then exit nonzero under ADR-0023 | Traced the interleaving against the drafted text | **Your decision:** sim stores `sim_state = CLOSED`, then `req_seq = UINT32_MAX - 1` (reserved), then `FUTEX_WAKE`, so the kernel value compare catches it; HAL also re-checks before classifying expiry, close beats expiry; interleaving test. Range check tightens to `frame < UINT32_MAX - 1` everywhere. **Fixed** |

Remaining critical/major/minor: none reported by the verification pass. No finding rejected. Codex's
"approve" is a recommendation, not your approval.

## Joe's attention

Read first: `docs/decisions/0022`, `0024` (the protocol and failure text), then `0023`; then the §15 and §16 hunks
of `docs/architecture.md` (`git diff origin/main -- docs/architecture.md`) and plan Q18.

Flagged refinements you already accepted, and two consequences of round 1 worth a second look:
1. **Two single-writer state words** (`sim_state`, `peer_state`) instead of one shared word.
2. **ADR-0023 covers the HAL's wait as well as the sim's** (two clock sites; `hal_now` stays frame-derived).
3. **Limit violation, throttle mismatch, and (new) non-finite command halt `kTransport` naming the ICD field.**
4. **PR 3 row cites "0022, 0023"** (a small extension of your instruction).
5. **Consequence of F3:** `UINT32_MAX - 1` is now reserved, so the last legal frame is `UINT32_MAX - 2`. The
   frame counter is the 1 kHz major frame, so 2^32 - 2 frames is about 49.7 days of sim time (the exchange count
   is a tenth of that); irrelevant to M4's 100 s runs, but it is a wire-format constant.
6. **Consequence of F2:** the limit check and the finite check duplicate ADR-0017's finite check on the same
   values; kept deliberately (the plan's R5 wording changed; ADR-0017 is not edited).

Assumptions and gaps: the exact `Transport` interface, header offsets, and config spelling are left to PR 3 and
PR 4 "under the ADR". The two interleaving tests need test hooks in the shm code; PR 4 must supply them. The
plan's own status line ("all questions closed") predates Q18; Q18 is appended, not retroactive.

Rollback: docs only. Reverting the squash commit restores the previous ADR set; nothing depends on these ADRs
until PR 3 merges. ADR-0007 and ADR-0010 are unchanged until the flip commit.

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: not requested

Significant tradeoff rationale: accepted a slightly larger surface in ADR-0022 (a local finite check that
duplicates ADR-0017's) to make the halt independent of a configurable health action; reserved one more
sequence value so shutdown wakes through the word the HAL actually waits on.
Accepted risks: none yet; the futex-ordering validation gap (plan Q14) is already accepted. Owner Joe;
trigger: PR 4's echo stress test and review.
Nonblocking follow-ups: none new. Existing: PLAN.md duplicate §15 bullets (PR 7).
Architectural decisions: [ADR-0022](../decisions/0022-lockstep-exchange-stage.md),
[ADR-0023](../decisions/0023-transport-timeout-clock-exception.md),
[ADR-0024](../decisions/0024-shm-layout-header-and-protocol.md), all **proposed**.

Remaining steps: your decision here; re-arm `.claude/touch-goldens`; the flip commit sets the three ADRs to
`Status: **accepted**, <date>`, fills each Approval with a citation of this Decision record, and adds the
"**Superseded in part by ADR-00NN** (<date>): ..." banners under the Status lines of ADR-0007 (to 0024) and
ADR-0010 (to 0023); rerun A1; then push `worktree-m4-pr1-adrs` explicitly (its upstream is `origin/main`, so
never a bare `git push`) and open a draft PR.
