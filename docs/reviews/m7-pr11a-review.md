# Review: M7 PR 11a, the sim-side F16_control peer and cases 13.1 to 13.4

Spec/plan: [docs/plans/m7-pr11a-control-peer.md](../plans/m7-pr11a-control-peer.md); the M7 plan's
[R4, R6, R9 to R12, R22, R23](../plans/m7-f16-nesc-oracle.md)
Builder: Claude (claude-opus-5-5)
Reviewer: Codex (companion 1.0.6; fresh context): general review, thread
`01a138e9-e3ed-75d2-9c62-2f69c8fca765`; adversarial review, thread `01a138e9-e3fd-7ba0-bf2c-115fa17ba29a`
Scope: the committed branch diff, base `86b17ce` (origin/main) to head `b813daa`, with a clean worktree
Validation: `./scripts/check-full.sh` on `09cf396` exit 0, `check: OK`
(`docs/reviews/evidence/09cf396-gate-summary.log`); `b813daa` adds evidence only
Round: fix 1

## Finding [R-001]

Severity: minor (Codex P2)
Status: fixed-unverified
Location: `aircraft/f16/sim/transport/nesc_control/nesc_control_peer.hpp:280-281` at `b813daa`
Requirement/ADR: AGENTS.md, "a sim that runs from a broken setup is worse than one that refuses";
docs/architecture.md §15 (unknown keys are errors)
Failure: a `transport.peer.schedule` that is a scalar, null, quoted string or map has `Config::size()`
0, so `read_schedule` reads no entry, and `Config::at()` has already marked the key consumed, so the
unknown-key check cannot catch it. Codex reproduced 13.1 with a quoted list: the run completed through
frame 5000 with no altitude-command change.
Requested outcome: refuse a non-sequence schedule at init with a `ConfigError` naming the key.
Builder response: fixed. `read_schedule` throws `ConfigError(origin, "peer.schedule", "must be a
sequence of schedule entries")` unless `schedule.is_sequence()`.
`F16Control.InitRefusesMissingOrOutOfRangeKeys` adds four cases (`5000`, `~`, a quoted list, a map),
each expected to throw naming `peer.schedule`. The tracked configs' sequences are unchanged.
Verification: pending (Codex fix verification).
History: round 1, open → fixed-unverified.
Human exception if needed: none.

## Completion

The adversarial review's verdict is approve, with no material findings. The general review raised
R-001 only. Open: R-001, awaiting verification. Codex inspected the committed full-gate result and did
not rerun it. Its ASan processes exited 1 under the sandbox, because ptrace blocked LeakSanitizer, but
their assertions passed. The reviewer's recommendation is not Joe's approval.
