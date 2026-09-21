# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**PR #40 squash-merged by Joe** (`6590b7c`, 2026-09-20): ADR-0019/0020/0021 + PR 3d on `origin/main`.

**M3 PR 4 implemented this session**, plan approved (Revision 2): `aircraft/notional_transport/
physical/aero` + `hardware/propulsion/turbofan`, `wrench_b()`-direct per ADR-0020, mirrored into
`aircraft/_template/`. Commits: `fecdda8` (plan) → `08b4d2d` (impl) → `2b157eb` (Codex general-review
fixes, verified clean by adversarial review) → `990864a` (Joe's own review: fixed a real One
Definition Rule hazard — both aircraft's `aero_math.hpp`/`turbofan_math.hpp` shared one generic C++
namespace; under `ASTRA_STATIC_PLUGINS` this silently corrupts whichever plugin the linker doesn't
keep once a copied aircraft's math is edited — plus `name()`/throttle-comment fixes) → `8b77c24`
(Codex fix-verification found the fix had a gap: naming a new aircraft `template`, no underscore,
made `new-aircraft.sh`'s own namespace rewrite a no-op; closed, plus a namespace-uniqueness test).

**Correction rounds: 2/2 used, per the workflow's own cap — stopping here, one finding open.**
Codex's *second* fix-verification pass (against `8b77c24`) found one more real, medium-severity
issue: `docs/architecture.md` §18 step 1 still documents plain `cp -r aircraft/_template
aircraft/my_aircraft` as equally valid to `just new-aircraft`, but only the latter rewrites the C++
namespace token — a user following the documented manual-copy path who then edits their aircraft's
math (step 2's own instruction) would silently recreate the exact ODR hazard just fixed. **Not
fixed this session — the round cap was reached; this is Joe's call**, not a third autonomous round.

## Last verified result
Full gate (`./scripts/check-full.sh`): **`check: OK`**, exit 0, this session, on `8b77c24` (from a
from-scratch build, `build/` removed first) — four presets × 362/362 tests each, 0 clang-tidy
findings, 0 pyright errors, 41/41 pytest. Retained:
`docs/reviews/evidence/{990864a,8b77c24}-full-gate.log`.

## Next step
Joe decides: accept `8b77c24` and track the §18 gap separately, or ask for one more fix round.
Human-review packet updated with the final candidate and this open finding. PLAN.md's PR 4 checkbox
stays unchecked until merge. PR 4a (ADR-0021) remains independent, not started.

## Tried / failed
Correction rounds: 2/2 used (round 1: include-path + finiteness fixes, verified clean; round 2:
ODR-namespace fix + its own follow-up gap, one open finding remaining). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
