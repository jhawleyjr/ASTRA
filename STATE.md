# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**PR #40 squash-merged by Joe** (`6590b7c`, 2026-09-20): ADR-0019/0020/0021 + PR 3d on `origin/main`.

**M3 PR 4 implemented this session**, plan approved (`docs/plans/pr4-notional-aero-turbofan.md`,
Revision 2): `aircraft/notional_transport/physical/aero` + `hardware/propulsion/turbofan`
(linear-coefficient aero, linear thrust-lapse turbofan, `wrench_b()`-direct per ADR-0020), mirrored
generically into `aircraft/_template/`; `aero_math.hpp`/`turbofan_math.hpp` give each plugin a pure,
hand-testable `compute_wrench()`. `cruise.yaml` (both aircraft) and `docs/architecture.md` §13/§7
updated to match. Commits: `fecdda8` (plan), `08b4d2d` (implementation), `2b157eb` (review fixes).

A build-time bug found only by running the build: `wing_area_m2` was rounded to 108.789460 instead
of the exact 108.78945984, failing the plan's own 1e-9 tolerance by ~1e-5 -- corrected everywhere.

Codex review-cycle (base `6590b7c`): general review found two real defects, both fixed in `2b157eb`
-- (1) `#include "aircraft/.../aero_math.hpp"` by full path meant a `just new-aircraft`-copied plugin
would silently keep including `_template`'s header, never its own edited copy; fixed to a
sibling-relative include (`plugins/physical/wind/dryden/dryden.cpp`'s own precedent). (2)
`checked_positive()`'s `value > 0.0` passed `+Inf`, silently zeroing thrust instead of failing
loudly; fixed to also require `std::isfinite()`, with new regression tests. Adversarial review
(frames/signs, SI units, V→0 algebra, ADR-0016 purity, allocation, R-001, test-literal independence,
template parity): **verdict approve, no material findings**, independently reproduced the literals.

## Last verified result
Full gate (`./scripts/check-full.sh`): **`check: OK`**, exit 0, this session, on `2b157eb` (final,
post-review-fix) -- four presets × 361/361 tests each, 0 clang-tidy findings, 0 pyright errors,
38/38 pytest. Log: `/tmp/check-full-pr4-3.log`.

## Next step
Prepare the human-review packet (`/human-review`) and hand off to Joe for review/merge. PLAN.md's PR
4 checkbox stays unchecked until Joe actually merges (matching PR 3d's own precedent). PR 4a
(ADR-0021) remains independent, not started.

## Tried / failed
Correction rounds: 1/2 used this session (the general-review fix batch, both findings fixed and
adversarial-verified in one round). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
