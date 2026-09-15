# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing once PR #21 (branch `worktree-m1-health`) merges. It adds `HealthMonitor` (`core/kernel/`,
docs/architecture.md §15): NaN/Inf in state or in any wrench contribution (per-contribution and
accumulated-total, naming the offending plugin or `<vehicle>.wrench_total`), quaternion norm drift
> 1e-6 before renormalization -- each check `off`/`warn`/`halt` (default `halt`) via an optional
`sim.health` config block, each a registered `health.*` signal. `WrenchSource` (`core/interfaces/`)
is a new `Model` subclass a `vehicle.models` entry can implement; `Vehicle` resolves every instance
once at init (`Eom::set_applied_wrench` had no caller before this PR). `Simulation::step()`/`run()`
now return a `HaltReason` (allocation-free struct); a halt still renormalizes q_nb (ADR-0001) before
returning. `tests/support/alloc_counter` replaces global operator new/delete so
`tests/physics/no_alloc_test.cpp` asserts zero allocations, linked only into `astra_physics_tests`.

## Last verified result
- `just check` exited 0 (`check: OK`) on every preset, 107/107 tests each (run three times: after the
  implementation, the Codex-review fixup, and the adversarial-review doc fixup).
- `/codex:rescue` plan round: 5 findings, 3 accepted and folded in before code existed
  (halt-before-renormalize was sharpest); 2 rejected with reasons in the plan file.
- Codex review: 1 finding (finite wrench contributions can overflow to Inf on summation) — fixed
  with a regression test.
- Codex adversarial review: 1 finding (the allocation counter misses a direct malloc/calloc call) —
  verified no plugin calls the C allocator directly, documented the scope limit rather than
  overriding malloc/free under ASan (real conflict risk, no current benefit); Joe may want a look.

## Next step
M2 Environment (PLAN.md): no PR breakdown exists yet (M1's four PRs are the model). Draft M2 PR 1 in
plan mode; natural first slice is ISA-1976 atmosphere plus a selectable round/WGS-84-rotating Earth
model (M1 only built flat). No precondition on Joe's machine.
Opener (model and effort pinned in `.claude/settings.json`; escalation is Joe's):
   `claude --worktree m2-environment --permission-mode plan`

## Tried / failed
Trusted the Bash tool's "completed" notification for `nohup just check > log 2>&1 &` under
`run_in_background: true`; failed because that double-backgrounds the command, so the notification
fires for the wrapper's near-instant exit, not the gate's. Fixed by finding the real `just` pid and
blocking on it directly (`tail --pid=<pid> -f /dev/null`); see `docs/lessons.md`.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
