# STATE (rewritten each session, never appended; ≤ 40 lines; written as true right now, not as of some future merge)

## In flight
PR #29 (branch `worktree-m2-nesc-pr3`), open, awaiting Joe's read and merge: M2 PR 3a, per
PLAN.md's own PR 3a bullet (the full content lives there, not duplicated here) -- populates
`EnvironmentSample::atmosphere`, `Vehicle::EnvironmentAvailability::atmosphere`, `round`'s
`rotating: <bool>`, new `plugins/physical/wind/table`, new `tests/plugins/nesc_aero`. Split from
the plan's own PR 3 line into 3a/3b, both unchecked until each merges, one-PR-per-session. Codex
review clean; adversarial review's one needs-attention finding (atmosphere's NaN misattributed
to the consuming `WrenchSource`, same shape as wind's own disclosed gap) is disclosed in
`environment_sample.hpp`/`atmosphere_model.hpp` and pinned by
`SimulationHalt.NanAtmosphereInRangeHaltsNamingTheWrenchConsumerNotTheAtmospherePlugin` (PR 2c's
own precedent: a disclosed gap moves visibly, not silently). Nothing else awaits Joe.

## Last verified result
`just check` exited 0 (`check: OK`, every test) on every preset, run four times this session
(implementation, formatting, the disclosure commit, the regression-test commit) -- the final
run, at the branch's final commit, is the one that matters.

## Next step
M2 Environment (PLAN.md) PR 3b: the NESC check-case harness, per PLAN.md's own PR 3b bullet,
which now carries every settled decision (ADR-0014's floor, the sampling fix, the t=0 exclusion,
case 8's node placement, the ten configs) and is the record that survives even if
`/home/joe/.claude/plans/m2-pr-3-nesc-hazy-fiddle.md` is pruned. Precondition: PR #29 merged
first; then `touch .claude/touch-goldens` before any `tests/golden/`/`docs/decisions/` file (the
new ADR-0014). First step, before any code: time `nesc_case1_test.cpp` once and multiply by ten.
Opener: `claude --worktree m2-nesc-pr3-b --permission-mode plan`

## Tried / failed
`WindTable`/`NescAero` unit tests first failed: a helper returned `VehicleState` by value from a
function-local `StateVector`, a dangling reference the instant it returned -- fixed by returning
`StateVector` and constructing `VehicleState` at the call site (`earth_kinematics_test.cpp`'s
pattern). A `wind/table` test failed from mixing units (metres/ft-s) -- fixed by converting to
SI at the config boundary. The disclosed-gap regression test's first design ("vehicle starts
outside valid_range()") was unreachable (`from_config` rejects it at init, ADR-0012 halts
mid-run first) -- fixed with a broken `AtmosphereModel` returning NaN *inside* its own range.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
