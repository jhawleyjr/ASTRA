# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**PR #40 squash-merged by Joe** (`6590b7c`, 2026-09-20): ADR-0019/0020/0021 + PR 3d on `origin/main`.

**M3 PR 4 implemented this session**, plan approved (Revision 2): `aircraft/notional_transport/
physical/aero` + `hardware/propulsion/turbofan`, `wrench_b()`-direct per ADR-0020, mirrored into
`aircraft/_template/`. Commits: `fecdda8` (plan) → `08b4d2d` (impl) → `2b157eb` (Codex general-review
fixes) → `990864a` (**Joe's own review**: fixed a real One Definition Rule hazard — both aircraft's
`aero_math.hpp`/`turbofan_math.hpp` shared one generic C++ namespace, a silent link-time collision
under `ASTRA_STATIC_PLUGINS`) → `8b77c24` (Codex fix-verification: the fix had a gap — naming an
aircraft `template` made the namespace rewrite a no-op; closed).

**Correction rounds: 2/2 used this session (unchanged).** A third finding (Codex's *second*
fix-verification, against `8b77c24`: `docs/architecture.md` §18 still documented plain `cp -r` as
equally valid to `just new-aircraft`, though only the latter rewrites the namespace) was reported as
open rather than fixed autonomously, since the two-round cap was already spent. **Joe explicitly
authorized one bounded fix for it, out-of-band from the cap** (his own instruction: "does not reset
the 2/2 budget; record it in STATE as Joe-authorized"): §18 now names `just new-aircraft` as the only
route with a clause on why; the superseded `990864a-full-gate.log` deleted; `docs-check` passed;
Codex verified the delta (one `P2` — stale evidence references to the deleted log — fixed in the same
pass). **All findings closed.**

## Last verified result
Full gate: **`check: OK`**, exit 0, on `8b77c24` (from-scratch build) — four presets × 362/362
tests, 0 clang-tidy, 0 pyright, 41/41 pytest. Retained: `docs/reviews/evidence/8b77c24-full-gate.log`
(`990864a`'s own log deleted per Joe, a strict subset). The Joe-authorized §18 fix is docs-only; no
full-gate rerun needed (`docs-check: OK`, 24 files, this session).

## Next step
Push, open the draft PR, mark ready. PLAN.md's PR 4 checkbox stays unchecked until squash-merge; PR
4a (ADR-0021) remains independent, not started.

## Tried / failed
Correction rounds: 2/2 used (round 1: include-path/finiteness fixes; round 2: ODR-namespace fix +
follow-up gap). Plus one Joe-authorized fix outside the round budget (§18 + log cleanup). All
closed. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
