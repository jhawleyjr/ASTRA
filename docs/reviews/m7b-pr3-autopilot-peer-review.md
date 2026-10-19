# Review: M7b PR 3, oracle (b), the port pattern, proven on M4's autopilot

Spec/plan: [docs/plans/m7b-pr3-autopilot-peer.md](../plans/m7b-pr3-autopilot-peer.md); the
[M7b plan](../plans/m7b-closed-loop-oracles.md), oracle (b); ADR-0029 Decision 8; ADR-0034.
Builder: Claude (Opus 5.5).
Reviewer: Codex, companion 1.0.6 (Codex CLI 0.160.0), its default model. Fresh context, read-only, two passes:
- the general `review --base origin/main`, thread `01a152cd-ce87-79d3-8265-cda428f63a4c`;
- `adversarial-review --base origin/main` with the PR's focus, thread `01a152ce-f120-7801-9616-7236471e1286`.

Scope: the branch diff `106c0cb..3ce60e1`, with a clean tree. The plan's own critique is in the plan's §16.
Validation:
- the full gate on `ca514b9`, exit 0 and `check: OK`, 1188 s ([summary](evidence/ca514b9-gate-summary.log));
- the acceptance run at `eab4adf` ([log](evidence/eab4adf-autopilot-peer-first-run.log)), after the first run at
  `704c339` ([log](evidence/704c339-autopilot-peer-first-run.log)), which stopped on a test's order comparison;
- the flags at `6da7622` ([log](evidence/6da7622-fp-flags.log)).

Round: initial, then fix 1.

**The general review:** no actionable defects. Codex ran both release test binaries (18 tests passed) and
`git diff --check`.

**The adversarial review:** verdict needs-attention, with one medium finding. It is verified and accepted. Codex
found no shipped-law mismatch.

## Finding [R-001]

Severity: major (Codex: medium)
Status: fixed-unverified
Location: `tests/autopilot_peer/autopilot_peer_equivalence_test.cpp:312-353` at `3ce60e1`,
`AutopilotPeerEquivalence.AntiWindupAtAndAcrossEachLimit`
Requirement/ADR: the plan's §8 item 2, which registers four cases for each of the three PIDs at each limit:
beyond the limit and winding further (held); exactly at the limit (inclusive, integrates); outside but nearer
(integrates); and an error sign change (unwinds at once). Also §8 item 3's 2026-10-18 deviation, which says this
test exercises the distance-decrease branch. ADR-0034 Decision 7.
Failure: two of the four cases were never reached, for any loop.
- The ramp's proportional term dominates, so every saturated step points outward and is refused. Codex's
  exact-order replay counted no outside-but-nearer step.
- The trim-at-limit block's zero-error frames leave the integral unchanged, and its nonzero errors land inside or
  outside a limit, never on it.
- The heading loop is never driven in that block.
- The seeded run's count of steps taken while saturated is 0 for speed and heading, so it does not fill the gap.
Requested outcome: deterministic port-against-peer PID sequences that land a nonzero step exactly on each limit,
and that stay outside but move nearer, with the integral change and each case's coverage asserted. Use direct
PID fixtures where the law's fixed offsets cannot reach a case.
Builder response: accepted and fixed in fix 1.
- **A further fact, verified by the builder.** For the speed and heading loops the branch is unreachable through
  the law. Their offset is constant and `kd` = 0, so a step that raises the integral is taken only if its command
  stays at or below hi. The integral then stays below hi − offset, and the loop is never outside a limit while its
  error pulls back; the same holds at lo. The altitude loop's pitch offset can sit past a limit, so it can reach
  the branch.
- **The fix**, inside the same test, keeping §8's 13 tests and the ctest count of 18:
  - direct `Pid` fixtures, `embedded/autopilot/pid.hpp`'s against the peer's, for each loop's gains, sign and
    range at each limit;
  - in each fixture, the four registered cases, each asserted: the integral held, or changed (to the exact
    candidate on the limit); the recomputed command beyond the limit, or on it; the output at the limit; and
    bitwise equal outputs and integrals;
  - a law-level witness for the altitude loop: θ one radian off the trim's, with a small error pulling back, at
    both limits. The integral moves while the elevator stays at the limit.
- **The plan.** §8 item 3's deviation gains a dated correction beside it, for Joe.
- **Validation.** `ctest --preset <p> -R AutopilotPeer`, with `-N` 18 first: 18 of 18 in asan, debug, release
  and static-plugins, exit 0. clang-tidy and clang-format are clean.
Verification: pending the Codex verification pass on the fix delta.
History: round 1, found by the adversarial review and fixed in the same round.
Human exception if needed: none. The correction to Joe's §8 wording goes to Joe at human review.

## Completion

To be completed after the verification pass.
