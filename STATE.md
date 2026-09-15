# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing once PR #16 (branch `worktree-m1-config`) merges. It adds `Config` (pimpl over yaml-cpp),
`SignalRegistry` (`v0.`-namespaced via `PrefixScope`), `VirtualClock` (`t = frame * dt`, ADR-0010),
`Scheduler` (ordered `(Model*, divisor)`, §5), `Vehicle`, `Simulation::from_config`/`run`. Configs use
one generic `vehicle.models` list (no typed `physical.`/`hardware.` slot exists yet); `vehicle:` is
singular per ADR-0011. ctest gained the `unit` label everywhere (`physics` is the convention, first
used in PR 3); Stop hook now runs `just test-unit` (`ctest -L unit`).

## Last verified result
- `just check` exited 0 (`check: OK`) on every preset, 71/71 tests each, in this session.
  `Simulation.RunsOneThousandFramesWithTExactlyFrameTimesDt`: 1000 frames, `t == frame * dt`
  bit-exact every frame; `v0.dummy.steps` == 1000, `v0.dummy.t` == `999 * dt` after `run()`.
- `/codex:review` found 3 real bugs (negative `sim.frames`/`divisor` wrapping to `UINT64_MAX`/
  `UINT32_MAX` on the unsigned cast; an untouched empty map/sequence bypassing `unknown_keys()`) —
  fixed with regression tests, not just noted.
- `/codex:adversarial-review` raised 3 findings, all rejected/deferred with reasons in the PR body:
  `Model::step` non-`noexcept` is PR 4's explicit acceptance item; SignalRegistry unit-vocabulary
  validation isn't an ADR-0002 requirement; the singular `vehicle:` config key is ADR-0011's own
  stated v1 shape.
- Found and fixed a real yaml-cpp bug: `current = child(current, segment)` in a navigation loop
  used `Node::operator=`, which mutates the aliased document in place instead of rebinding the
  handle — `Config::navigate` is now recursive (`docs/lessons.md`).

## Next step
M1 PR 3 (PLAN.md): `EarthModel` (flat), `GravityModel` (constant), `MassPropertiesModel` (constant),
`Eom` derivative (Stevens & Lewis) in the frame loop. Accept: `tests/physics/` torque-free
asymmetric body: energy/|H_n| drift < 1e-9 relative over 1000 s at 1 kHz; ballistic arc vs closed
form; symmetric-top precession vs closed form. Plan review: EOM/kinematics → Codex `/codex:rescue`
before code (unpinned design). No precondition on Joe's machine.
Opener (model and effort pinned in `.claude/settings.json`; escalation is Joe's):
   `claude --worktree m1-eom --permission-mode plan`

## Tried / failed
- Nothing unresolved this session; the yaml-cpp corruption bug and all Codex findings were fixed
  before opening the PR (see `docs/lessons.md`).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
