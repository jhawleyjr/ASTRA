# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
M4 PR 4 (shm transport `plugins/sim/transport/shm`, `embedded/hal` shm backend, shared layout header
`embedded/hal/astra_shm_layout.h`, echo peer, `tests/lockstep`), Tier 3, **approved by Joe 2026-09-26**: product
candidate `95369fe` on branch `worktree-m4-pr4-shm-transport` off `origin/main` `7a349ee` (#54; PR 3 is #53, merged).
Later commits are documents only. [Draft PR #55](https://github.com/jhawleyjr/ASTRA/pull/55), body = the packet
(verified with `gh pr view --json body`); stays draft, Joe squash-merges. PR 4 is merged once `origin/main` contains
it. Plan: `docs/plans/m4-pr4-shm-transport.md`. Packet: `docs/reviews/m4-pr4-shm-transport-human.md` (approved).

## Last verified result
On `95369fe`, this session: full gate `./scripts/check-full.sh` exit 0, `check: OK`, presets 510/511/510/510,
pyright 0 errors, pytest 116 (`docs/reviews/evidence/95369fe-full-gate.log`). A4 asan selector 60/60
(`95369fe-a4-asan.log`); `ctest --preset release -R EchoPeerStress` 1e6 exchanges, mean 2.86 us, p99 4.53 us
(`95369fe-a4-stress.log`). M3 frozen regression and PR 3 suites pass unmodified. Codex adversarial on `d020d57`:
needs-attention, R-1/R-2 accepted and fixed; round-1 verification on `d020d57..95369fe`: approve, no findings
(`d020d57-codex-adversarial.log`, `95369fe-codex-verify.log`). Mutation checks recorded in the plan. GitHub
`check` on #55 head `1f2568f`: success (local-gate mode, not a full gate).

## Next step
1. Joe squash-merges #55 (approved candidate `95369fe`). Next task, from a fresh worktree off the merged
   `origin/main`: M4 PR 5, `embedded/autopilot` (C++17, four PID loops),
   `embedded/CMakeLists.txt` + standalone `just embedded-standalone` (replace the top-level
   `add_subdirectory(embedded/hal)` line), autopilot `nm` boundary test, Q13 gain design (Tier 2).
2. Follow-up A (comment-only PR): PR 2's CAS table test comment nits (`docs/reviews/m4-pr2-icd-pack-human.md`).
3. Follow-up B (Tier 0 PR, authorized): citations in `util/frames.hpp` and `tests/golden/frames/adr0001_cases.hpp`
   comments (Joe arms the guard): DCM 1.3-10, Euler 1.3-11, Example 1.8-1 Eq. (1) p. 52, q to DCM 1.8-16/18.
4. Follow-up C (Joe, not scheduled): `LockstepExchange` recorder sizing for an absurd `sim.frames`.
5. Not scheduled: a wall-clock symbol scan allowlisting the two ADR-0023 clock functions (architecture §15).
PLAN.md untouched (M4 check-off and the duplicate §15 bullet: M4 PR 7). Joe-owned: the stale
`tests/golden/dynamics/capture_r1_golden.cpp` (retired `astra::Eom` symbols, ADR-0021) needs his authorization.

## Tried / failed
Correction rounds: 1/2; failed integrations: 1/2 (clang-tidy on `937868c`: a NOLINT moved by clang-format,
`937868c-full-gate-failure-summary.log`; lesson 2026-09-26). A new defect goes to Joe.
Coverage gaps disclosed in the packet: the plugin side of R-1 has no deterministic regression (dlopen'd, `--wrap`
cannot reach it); an `mmap` failure in `open()` is review-only; M4 Q14 memory-order detection gap.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
