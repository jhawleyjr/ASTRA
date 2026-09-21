# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**PR #40 squash-merged by Joe** (`6590b7c`, 2026-09-20): ADR-0019/0020/0021 + PR 3d on `origin/main`.

**M3 PR 4 implemented this session**, plan approved (Revision 2): `aircraft/notional_transport/
physical/aero` + `hardware/propulsion/turbofan`, `wrench_b()`-direct per ADR-0020, mirrored into
`aircraft/_template/`. Key finding, **Joe's own review**: both aircraft's `aero_math.hpp`/
`turbofan_math.hpp` shared one generic C++ namespace — a silent link-time One Definition Rule
collision under `ASTRA_STATIC_PLUGINS` the instant a copied aircraft's math is edited (empirically
confirmed: clean compile/link, then an ASan-detected crash). Fixed with per-aircraft namespaces, a
`new-aircraft.sh` rewrite, and a static-plugins regression test. Two Codex fix-verification rounds
closed two follow-up gaps in that fix (an aircraft named `template` bypassed the rewrite; §18 still
documented a `cp -r` route that skips it — **Joe explicitly authorized this last fix out-of-band**,
"does not reset the 2/2 budget"). **All findings closed.** Full history: 11 commits, `fecdda8`
(plan) through `7d15c62` (final); see `docs/reviews/pr4-notional-aero-turbofan-human.md`.

**Pushed and marked ready for review, per Joe's own instruction**: PR #41
(https://github.com/jhawleyjr/ASTRA/pull/41), head `7d15c62`. No further action taken.

## Last verified result
Full gate: **`check: OK`**, exit 0, on `8b77c24` (from-scratch build) — four presets × 362/362
tests, 0 clang-tidy, 0 pyright, 41/41 pytest. Retained: `docs/reviews/evidence/8b77c24-full-gate.log`
(`990864a`'s own log deleted per Joe, a strict subset). `e3b3f6e`'s docs-only follow-up needed no
full-gate rerun (`docs-check: OK`, 24 files).

## Next step
Joe squash-merges himself; not yet merged as of this handoff. PLAN.md's PR 4 checkbox stays
unchecked until then. PR 4a (ADR-0021) remains independent, not started.

## Tried / failed
Correction rounds: 2/2 used (round 1: include-path/finiteness fixes; round 2: ODR-namespace fix +
follow-up gap). Plus one Joe-authorized fix outside the round budget (§18 + log cleanup). All
closed. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
