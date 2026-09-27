# Review: M5 PR 1, ADR-0025/0026 and architecture rev 9

Spec/plan: [docs/plans/m5-tooling.md](../plans/m5-tooling.md), PR 1 row (Q3 to Q13, Q23)
Builder: Claude Opus 5.5
Reviewer: Codex (companion 1.0.6, `adversarial-review`, fresh context, read-only)
Scope: committed branch diff `2cce400..a63568d` (ADR-0025, ADR-0026, `docs/architecture.md`, `AGENTS.md`,
`PLAN.md`); the worktree was clean
Validation: `python3 scripts/docs_check.py` exit 0; `git diff --check` exit 0; no build or test (docs only)
Round: fix 1

## Finding [F1]

Severity: major (Codex: medium; a contract conflict inside an approved decision)
Status: fixed-unverified
Location: `docs/decisions/0025-hdf5-run-log-and-replay.md` Replay paragraph (lines 93 to 97 at `a63568d`)
Requirement/ADR: plan Q4, Q7, Q11; ADR-0025 Failure, Provenance and Replay
Failure: replay requires every halted log to reproduce its halt with one more `step()`. The stored config
omits `log`, so `buffer_frames` is not recorded, and neither is the drain schedule. Example: a `step()`-driven
run with `buffer_frames=2` and no `flush_log()` halts `kLog` before frame 2. The replay runs with the default
4096 rows and completes that frame. A failed drain write cannot be reproduced from the config and writes at all.
Requested outcome: Joe decides between refusing `kLog`-halted logs and recording enough metadata to reproduce them.
Builder response: verified against the ADR text. Joe decided on 2026-09-27: refuse, with a named reason. Changes:
- ADR-0025 Replay: refuses a `kLog`-halted log and states that every other halt still replays. A rejected
  alternative (recording `buffer_frames` and the drains) was added, and R9's validation line now names the
  refusal.
- `docs/architecture.md` §9 Replay: the same refusal.
- `docs/plans/m5-tooling.md`: the replay contract, R9, and a Q11 entry under "Joe's decisions".
Verification: pending (Codex fix verification)
History: initial review, open; fix 1, fixed-unverified
Human exception if needed: not an exception; this is Joe's requirement decision (2026-09-27)

## Completion

Initial pass: one finding (F1), no others. After the fix: critical/major open count pending verification.
