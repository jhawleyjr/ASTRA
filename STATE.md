# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing. M2 PR 3a (branch `worktree-m2-nesc-pr3`, PR #29) merged: populates
`EnvironmentSample::atmosphere` in `Simulation::step` (one shared `earth_->altitude(vs)` feeds
both `wind_n` and `atmosphere`); `Vehicle::EnvironmentAvailability::atmosphere` (conditional
rejection, not unconditional); `plugins/physical/earth/round`'s `rotating: <bool>` (case 5); new
`plugins/physical/wind/table` (altitude-keyed wind, case 8); new `tests/plugins/nesc_aero`
(sphere drag + brick damping `WrenchSource`, both forms checked against NASA/TM-2015-218675 Vol
II's own values; `requires_wind` config-driven so it doesn't reject the eight NESC cases with no
`physical.wind`). Split from PLAN.md's M2 PR 3 line, one-PR-per-session: PR 3b (the harness,
goldens) is separate. Codex review clean; adversarial review's one needs-attention finding
(atmosphere's NaN misattributed to the consuming `WrenchSource`, same shape as wind's own
disclosed gap) is disclosed, not fixed, in `environment_sample.hpp`/`atmosphere_model.hpp`.

## Last verified result
`just check` exited 0 (`check: OK`, every test) on every preset, run three times this session
(implementation, formatting, the disclosure commit) -- the final run, at the branch's final
commit, is the one that matters.

## Next step
M2 Environment (PLAN.md) PR 3b: the NESC check-case harness. Plan already approved this session:
`/home/joe/.claude/plans/m2-pr-3-nesc-hazy-fiddle.md` -- read it first (the geoid-gap measurement,
guard-lift sequence, ADR-0014 floor proposal, the ten configs, and the read/`run(1)`/read-wrench/
`run(99)` sampling fix, not a plain `sim.run(100)`). Precondition: the plan's own first
implementation step, timing `nesc_case1_test.cpp` once and multiplying by ten, is not yet done
(plan mode cannot run builds). Then `touch .claude/touch-goldens` before any `tests/golden/` or
`docs/decisions/` file (the new ADR-0014) can be written.
Opener: `claude --worktree m2-nesc-pr3-b --permission-mode plan`

## Tried / failed
`WindTable`/`NescAero` unit tests first failed: a helper returned `VehicleState` by value from a
function-local `StateVector`, a dangling reference the instant it returned (`VehicleState` holds
`const StateVector&`) -- fixed by returning `StateVector` and constructing `VehicleState` at the
call site, matching `earth_kinematics_test.cpp`'s pattern; caught by the gate, not review. A
`wind/table` interpolation test also failed from mixing units (altitude in metres, wind still in
ft/s) -- fixed by converting to SI at the config boundary (ADR-0002).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
