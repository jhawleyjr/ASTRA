# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**M3 PR 7** (`astra.linearize`: central-difference `A`/`B` about `astra.trim`'s cruise trim, five
classical modes checked against S&L §4.2) is **implemented, validated, and Codex-reviewed —
human-review packet prepared, awaiting Joe's decision**. Plan: `docs/plans/pr7-linearize.md`
(Revision 3, approved 2026-09-21). Candidate `8c974cd`; branch `worktree-m3-pr7-linearize`, **not
pushed, no PR opened**. Base `origin/main` at `53064e0` (confirmed current tip at packet time).
Includes PR 6's own follow-up (`Simulation.input_names()` bound, no `kCoreApiVersion` bump);
`astra.trim` itself unmodified (PLAN.md follow-up tracks that retrofit).

**D18 (mid-session revision)**: R4 first missed on 3/5 modes against real numbers — root cause
verified as a coefficient-reference mismatch (`aero_math.hpp` transfers Cm_alpha/Cl_beta/Cn_beta
AERORP→CG; S&L's derivatives are CG-referenced), fixed by re-deriving CG-referenced coefficients
in the test, not by loosening tolerances. Phugoid `omega_n` stays a real, expected miss
(`xfail(strict=True, raises=AssertionError)`; its damping half passes separately). PLAN.md's PR 8
line gets a follow-up: use PR 7's numerical `A` for its own phugoid cross-check, not S&L's formula.

Codex review (general + adversarial, base `53064e0`): R-001 (major, control values/steps assumed a
fixed position order instead of `Simulation.input_names()`'s own runtime order) and R-002 (medium,
phugoid `xfail` swallowed the damping assertion and any unrelated exception) — both fixed
(`0fa5e85`), fix-verification **approve**, "New findings: none". Findings:
`docs/reviews/pr7-linearize-review.md`. Packet: `docs/reviews/pr7-linearize-human.md`.

## Last verified result
Full gate `check: OK` at `009715a` (pre-fix), `docs/reviews/evidence/009715a-full-gate.log` — hooks
144/144, four presets 387/387, pyright 0, pytest 97 passed + 1 expected `xfail`. Re-run after the
fix batch (`0fa5e85`) this session: same shape, 98 passed + 1 expected `xfail`, not separately
retained as a log.

## Next step
**Joe's review and decision.** Nothing further for Claude unless he requests changes or asks to
push/open a PR. After approval: push, open draft PR for GitHub's own `check`. After merge: check
off PLAN.md's PR 7 line with the merge SHA.

## Tried / failed
Correction rounds: 1/2 (the Codex review fix batch above). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
