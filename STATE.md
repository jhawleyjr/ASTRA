# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
PR #27 (branch `worktree-m2-environment-2c`), open, awaiting Joe's read (physics diff) and merge.
Two parts: (1) docs-only commit landing ADR-0013 (NESC oracle: ensemble band, Python-generated,
human-owned) and restoring `AGENTS.md` as a symlink to `CLAUDE.md` (PR #7's original design, which
PR #11 inverted) -- one file, 100 lines, `workflow.md`/`SKILL.md` updated to match. (2) M2 PR 2c:
ADR-0012 per-frame domain validity -- `EarthModel::in_domain`/`GravityModel::in_domain` (incl.
`wgs84_j2`, whose `validate_p_n` was never a no-op), `HealthCheck::kOutOfDomain` post-step with a
one-frame lookahead plus a frame-0 pre-step check, the atmosphere `valid_range()` check moved into
`Simulation::step`. `kCoreApiVersion` bumped 3 -> 4 (Codex review: `in_domain` changes the
Earth/Gravity vtable). **Known gap, disclosed not fixed** (Codex adversarial review, `PLAN.md`'s PR
2c line and `Vehicle::check_domain`'s own comment): the lookahead is first-order in velocity: RK4's
own stages nest acceleration into position at `0.5*|a|*dt^2` (~4.9 um at standard gravity, dt=1ms),
so a state within that band at near-zero velocity can have a stage evaluate singular math first;
the NaN/Inf state check remains the backstop. Pinned by a unit test so the band's size moves
visibly, not silently, if `dt`/the integrator change. Nothing awaits Joe but the read and merge.

## Last verified result
`just check` exited 0 (`check: OK`, 253/253 tests) every preset, this session, at HEAD (`dee4c86`)
-- run once per commit across this PR's four; the final run is the one that matters.

## Next step
M2 Environment (PLAN.md) PR 3: NESC check-case harness, oracle pipeline now settled by ADR-0013
(cases 1-10 from Table 6.3-1; case 5 needs `rotating: true`; case 8's wind is an altitude-keyed
`plugins/physical/wind/table`, not time-varying; the geoid gap is now load-bearing for all eight
WGS-84 cases, MSL vs. ellipsoidal). Precondition on Joe's machine: PR #27 merged first (M2 PR 3's
own line requires ADR-0012 landed, since cases 9-10 fly latitude-varying trajectories); Joe places
NASA/TM-2015-218675's data under `docs/refs/` (gitignored) before the generator can run.
Opener: `claude --worktree m2-nesc-pr3 --permission-mode plan`

## Tried / failed
`Vehicle::check_domain`'s `eom_->earth()/gravity()` accesses failed clang-tidy's
`bugprone-unchecked-optional-access` despite the caller's own guard (its invariant doesn't cross
the function boundary) -- fixed with a local `has_value()` guard, one extra `just check` cycle.
Codex adversarial review's first pass (needs-attention) on the RK4-lookahead gap above cost a
second commit (disclosure + pinning test) after Joe's explicit direction on how to resolve it.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
