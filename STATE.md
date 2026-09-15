# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing once PR #19 (branch `worktree-m1-eom`) merges. It adds `EarthModel`/`GravityModel`/
`MassPropertiesModel` interfaces (each with an out-of-line destructor in `libastra_core`, the same
key-function fix `Model` already needed, so `dynamic_cast` and UBSan's vptr check work across every
plugin's `-fvisibility=hidden` boundary), `VehicleState`, `Eom` (Stevens & Lewis, parameterized by the
Earth interface), and the first three plugins outside `tests/plugins/`:
`plugins/physical/{earth/flat, gravity/constant, mass/constant}`. `Simulation` gains typed
`physical.earth`/`physical.gravity`/`physical.mass` config slots and `initial.state`; physics is
opt-in (all three slots or none), so PR 2's dummy-plugin fixture is unchanged. Also fixes a
pre-existing `Config::navigate` bug: a bare `YAML::Node{}` is Null-typed but still `IsDefined()`, so
navigating a dotted path through a scalar never stopped as intended (`docs/lessons.md`).

## Last verified result
- `just check` exited 0 (`check: OK`) on every preset, 89/89 tests each, in this session (run twice:
  after the implementation, and again after the Codex-review fixup commit).
- `tests/physics/`: torque-free body, energy/`|H_n|`/`H_n` direction all < 1e-9 relative drift over
  1000 s at 1 kHz; ballistic arc at two attitudes (`v_b` checked independently of the trajectory);
  symmetric-top precession vs closed form.
- `/codex:rescue` plan round: 3 findings, all accepted — a wrong-sign precession closed form and two
  oracle tests that didn't catch what they claimed to. Folded in before any code existed.
- Codex review + adversarial review: 3 findings (non-finite `physical.gravity.g`, non-finite
  `initial.state` components, a non-map `initial.state` silently accepted) — fixed with regression
  tests in a follow-up commit, not just noted.

## Next step
M1 PR 4 (PLAN.md): `HealthMonitor` — NaN/Inf in state or any wrench (plugin named), `|q|` drift > 1e-6
before renormalization; `Simulation::run` returns a typed halt reason, no throw in the loop;
allocation counter in the asan preset. Accept: a NaN wrench at frame 500 halts naming it; `q` norm
1.1 halts naming `v0.state.q_nb`; zero allocations over 1000 frames. Plan review: health monitor →
Codex (`/codex:rescue`) before code. No precondition on Joe's machine.
Opener (model and effort pinned in `.claude/settings.json`; escalation is Joe's):
   `claude --worktree m1-health --permission-mode plan`

## Tried / failed
Nothing unresolved this session; both Codex review rounds' findings were fixed before opening the PR.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
