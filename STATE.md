# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 3 (Tier 3): PR #65, branch `worktree-m5-pr3-derived-signals` off `origin/main` `82686ec`, plan
`docs/plans/m5-tooling.md` PR 3 row (Q13; R1, R5, R10), binding ADR-0026. Stage: human review; the PR body is
the description Joe reviews. Code candidate `1c1438c`; later commits change no code. The stage is
`core/include/astra/kernel/derived_signals.hpp` and `core/src/kernel/derived_signals.cpp`, wired into
`Simulation::step_frame` (a `pack()` after each vehicle's environment sample, a `note_completed()` before
`clock_.advance()`), plus `Vehicle::has_physics()`. `ax ay az` come from a wrench and mass snapshot taken at
frame completion (Codex F1). `lockstep_exchange.cpp` is untouched. Tests: `tests/unit/kernel/derived_signals_test.cpp`
(the `DerivedSignals` suite) and one NoAlloc case. Review record: `docs/reviews/m5-pr3-derived-signals-review.md`.

## Last verified result
This session, at `1c1438c`: `./scripts/check-full.sh` exit 0 with `check: OK`
(`docs/reviews/evidence/1c1438c-gate-summary.log`). The plan's acceptance regex under `asan` exits 0, 87/87;
`git diff --exit-code d3e82e3 -- core/src/kernel/lockstep_exchange.cpp` exits 0. R10: the recorder SHA-256 of
release builds of `d3e82e3` and `1c1438c` is identical on three loopback configs; the hashes are in the gate
summary. Codex: general review clean; adversarial needs-attention (F1), then fix verification and refactor
verification both approve. Thread ids are in the PR body.

## Next step
1. Joe: decide on PR #65. Its "Needs Joe's decision" section asks for approval of `1c1438c` and a ruling on
   `pack()` sitting inside the per-vehicle loop for multi-vehicle `for_testing`. After any later push, rerun
   CI's `check` with `gh pr ready --undo` then `gh pr ready`.
2. M5 PR 4 (Tier 3): the logger stage, on PR 2 and PR 3. It logs `v<i>.derived.*` with ADR-0026's
   previous-frame `ax ay az` disclosed. PR 5 must check h5py's bundled libhdf5 alongside ours in one process.
3. Before PR 8: Joe places FlightGear's `net_fdm.hxx` (version 24) in `docs/refs/` (Q21).

## Tried / failed
Correction and integration counts: PR #65's body holds them. The gate at `7cf5a47` failed only on clang-tidy
(`step_frame` cognitive complexity 26 > 25); `1c1438c` builds the stage unconditionally instead of adding
branches. The R10 driver and configs stayed in the session scratchpad; the method is in the PR body. The
worktree guard refused `bash <script>`; scripts run by their path (lessons 2026-09-27).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
