# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 5 (`embedded/autopilot`, standalone embedded build, Q13 gain design), Tier 2, plan revision 2 **approved by
Joe 2026-09-26**: product and test candidate `56ea2ec` on branch `worktree-m4-pr5-autopilot` off `origin/main`
`34eec2d` (PR 4 #55, merged). Later commits are documents only (head `2db8989`). **Not pushed, no PR.** Stage: the
human-review packet is written and waits for Joe's decision; then push as a draft PR (body = the packet), Joe
squash-merges. PR 5 is merged once `origin/main` contains it. Plan: `docs/plans/m4-pr5-autopilot.md`. Packet:
`docs/reviews/m4-pr5-autopilot-human.md` (state pending).

## Last verified result
On `56ea2ec`, this session: `./scripts/check-full.sh` exit 0, `check: OK` (debug 553, release 554, static-plugins
553, asan 553 tests; hooks 144; pyright 0 errors; pytest 130; `docs/reviews/evidence/56ea2ec-full-gate.log`). A5 run
separately, all exit 0: `just embedded-standalone`, `ctest --preset asan -R '(Autopilot|EmbeddedBoundary)'` 43/43,
`pytest tests/python/test_autopilot_design.py` 14/14 (`56ea2ec-a5-*.log`). 15/15 mutations caught
(`56ea2ec-a5-mutations.md`). Gains: `evidence/m4-pr5-gain-design.md`. PR 3/PR 4/M3/PR 8 tests pass unmodified.
Codex review of `34eec2d..3dc9ca7`: no actionable regressions (`3dc9ca7-codex-review.log`); plan critique: 4
findings, fixed in the plan.

## Next step
1. Joe reads the packet and decides. If approved: push `worktree-m4-pr5-autopilot`, open a draft PR whose body is the
   packet, Joe squash-merges. Next task, from a fresh worktree off the merged `origin/main`: M4 PR 6 (turbulence
   configs, Python driver `astra.lockstep`, acceptance runs R1 to R4b, R12b). Its handoff (CLI incl. `--theta-trim` =
   `TrimResult.alpha_rad`, limits equal to the autopilot's, frame-0 caveat under Dryden, gust throttle traces,
   `kHeadingP`/bank limit first knobs) is at the end of the packet.
2. Follow-up A (comment-only PR): PR 2's CAS table test comment nits (`docs/reviews/m4-pr2-icd-pack-human.md`).
3. Follow-up B (Tier 0, authorized; Joe arms the guard): citations in `util/frames.hpp` and
   `tests/golden/frames/adr0001_cases.hpp` (DCM 1.3-10, Euler 1.3-11, Example 1.8-1 Eq. (1) p. 52, q to DCM 1.8-16/18).
4. Not scheduled: follow-up C (Joe: `LockstepExchange` recorder sizing for an absurd `sim.frames`); a wall-clock
   symbol scan allowlisting the two ADR-0023 clock functions (architecture §15).
PLAN.md untouched (M4 check-off and the duplicate §15 bullet: M4 PR 7). Joe-owned: the stale
`tests/golden/dynamics/capture_r1_golden.cpp` (retired `astra::Eom` symbols, ADR-0021) needs his authorization.

## Tried / failed
Correction rounds: 0/2; failed integrations: 1/2 (clang-tidy on `49e54b3`: five diagnostics in the new
`tests/embedded` files; fixed in `56ea2ec`; `49e54b3-full-gate-failure-tidy.log`). A new defect goes to Joe.
Coverage gaps disclosed in the packet: the design model has no saturation or turbulence (PR 6's R2 decides); margins
are loop-at-a-time; LeakSanitizer under ptrace (lesson 2026-09-17).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
