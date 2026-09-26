# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 5 (`embedded/autopilot`, standalone embedded build, Q13 gain design), Tier 2, plan revision 2 approved by Joe
2026-09-26. Joe's human review of `56ea2ec` was "not approved yet"; **correction round 1** (F1 yaw-damper sign, F2 slow
inner loop, airspeed <= 0) is done: product and test candidate `fc22cc8` on `worktree-m4-pr5-autopilot` off `origin/main`
`34eec2d`, head `a0eec8c` (evidence and review records only). **Not pushed, no PR.** Stage: the updated packet waits for
Joe's re-review; then push as a draft PR (body = the packet), Joe squash-merges. Merged once `origin/main` contains it.
Plan: `docs/plans/m4-pr5-autopilot.md`. Packet: `docs/reviews/m4-pr5-autopilot-human.md` (state pending). Findings:
`docs/reviews/m4-pr5-autopilot-review.md` (H-1, H-2, H-3, all verified).

## Last verified result
On `fc22cc8`, this session: `./scripts/check-full.sh` exit 0, `check: OK` (debug 555, release 556, static-plugins 555,
asan 555 tests; hooks 144; pyright 0 errors; pytest 135; `docs/reviews/evidence/fc22cc8-full-gate.log`). A5 separately,
all exit 0: `just embedded-standalone`, ctest `(Autopilot|EmbeddedBoundary)` 45/45, design test 19/19
(`fc22cc8-a5-*.log`). Mutations all caught (`fc22cc8-a5-mutations.md`). Codex verification of `56ea2ec..a0eec8c`:
approve, H-1/H-2/H-3 verified, new findings none (`a0eec8c-codex-verify.log`). PR 3/PR 4/M3/PR 8 tests unmodified.

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
Correction rounds: 1/2 (Joe's review of `56ea2ec`: H-1 to H-3, fixed in `fc22cc8`); failed integrations: 1/2 (clang-tidy
on `49e54b3`, fixed in `56ea2ec`; `49e54b3-full-gate-failure-tidy.log`). A new defect goes to Joe.
Coverage gaps disclosed in the packet: the design model has no saturation or turbulence (PR 6's R2 decides); margins
are loop-at-a-time and inner-loop bandwidths are modal; LeakSanitizer under ptrace (lesson 2026-09-17).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
