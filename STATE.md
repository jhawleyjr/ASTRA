# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 2 (ICD truth packing), Tier 3, **approved by Joe 2026-09-25**: candidate `3322d26` (product), evidence/packet
head `1e63268`. Branch `worktree-m4-pr2-icd-pack` off `origin/main` `a3d6071` (confirmed unmoved 2026-09-25),
[PR #52](https://github.com/jhawleyjr/ASTRA/pull/52), ready for review, awaiting Joe's squash-merge (not merged).
Scope: `core/include/astra/util/icd_pack.hpp`, `icd/astra_icd.h` (comments plus five `status_flags` `#define`s, R11
holds), `tests/unit/icd/icd_pack_test.cpp` (25 tests). Not wired into `Simulation` (M4 PR 3). Packet:
`docs/reviews/m4-pr2-icd-pack-human.md` (decision record: approved). Plan: `docs/plans/m4-lockstep.md`.
Docs-only wrap commits (STATE.md, one lesson) sit on top of `752436d`; the product candidate stays `3322d26`.

## Last verified result
Full gate `./scripts/check-full.sh` on `3322d26`: `check: OK`, `exit=0`, `docs/reviews/evidence/3322d26-full-gate.log`.
`ctest --preset asan -R IcdPack --no-tests=error`: 25/25. Codex adversarial (round 0) approve; fix-verification
(`c29c8d7`) needs-attention, one finding F-1 (CAS table anchor provenance), fixed in `3322d26`; fix-verification
(`77f75e4`) approve, no new findings. GitHub `check` (local-gate mode, not a full gate) passed on head `752436d`; re-triggered on the wrap head (see PR).

## Next step
1. Joe squash-merges PR #52. Then M4 PR 3 (core seam: `Transport`, exchange stage, `kCoreApiVersion` 8, Tier 3, own
   plan critique). PR 3 must fill `PreviousFrame` from the previous frame's published wrench and mass, null at frame 0.
2. Follow-up A (in one small PR, comment-only): CAS table test comment nits (`|g''|` 0.808; the 0.029 m/s gap is mostly
   the 0.43/0.44 row rounding ~0.026, .6560 ~0.007, interpolation ~-0.004; the "changed-gamma pair" mutant means matched
   exponents 3.4 and 1/3.4 with the 0.2/5 factors held, and a consistent gamma 1.41 moves CAS only 0.010 m/s).
3. Follow-up B (separate Tier 0 PR after merge, authorized): fix equation citations in
   `core/include/astra/util/frames.hpp` and the comments of `tests/golden/frames/adr0001_cases.hpp` (comment-only
   there; Joe arms the golden guard): DCM 1.3-10, Euler from DCM 1.3-11, Example 1.8-1 Eq. (1) p. 52, q to DCM 1.8-16/18.
PLAN.md is untouched (duplicate §15 bullets, lines 600/605, go in M4 PR 7).

## Open follow-up (not scheduled)
PLAN.md: `tests/golden/dynamics/capture_r1_golden.cpp` (R1/R1-Sim capture harness, M3 PR 4a) is
stale (retired `astra::Eom`/`post_step_renormalize` symbols, ADR-0021). Needs Joe's authorization —
it is under `tests/golden/**`.

## Tried / failed
Correction rounds: 2/2 (round 1: Joe's six items; round 2: Codex F-1, CAS table anchor). Failed integrations: 2/2
(two lint-only gate failures before the passing runs: `format-check` on `8413365`, `tidy` on `c439661`; logs
removed at Joe's request, rows kept in the packet). A new defect goes to Joe.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
