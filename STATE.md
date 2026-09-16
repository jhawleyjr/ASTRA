# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing once PR #25 (branch `worktree-m2-environment-2`) merges. Adds `WindModel`
(`begin_frame`/`wind_n`/`shareable_across_vehicles`, §7) and the per-vehicle `EnvironmentSample`
latch carrying wind (and, from PR 3, atmosphere) to a `WrenchSource`, plus
`plugins/physical/wind/uniform`. `physical.wind`/`physical.atmosphere` share one divisor check.
`Simulation::for_testing` (test-only, public on `Simulation`, accepted explicitly per `PLAN.md`'s
PR 2 line) exercises D1's "per vehicle" call contract and D4's shareability guard, since
`from_config` never builds more than one vehicle (ADR-0011); rejects partial physics inputs and a
non-null `wind`/null `earth` (reachable null-pointer bugs, closed this session), but does not
replicate `from_config`'s Config-driven validation (position-representation, `validate_p_n`,
atmosphere `valid_range`) by design. `kCoreApiVersion` bumped 1 -> 3.
**Disclosed, not fixed** (Codex + Cowork, re-affirmed twice): (1) `assumed_position_representation()`
declarative, not enforced, same as `GravityModel` since M2 PR 1. (2) first per-frame caller of
`EarthModel::altitude()`, PR 1's/1b's domain-validity gap re-accepted knowingly. (3) `HealthMonitor`
has no environment check, so a NaN `wind_n` halts naming the *consuming* `WrenchSource`, not wind.

## Last verified result
`just check` exited 0 (`check: OK`, 185/185 tests) every preset, every round this session. Codex
review + adversarial review, final round: no findings, approve. Two Cowork review rounds, 7
findings, all resolved or disclosed; two fixes (partial-physics and wind-without-earth null derefs
in `for_testing`) caught only by re-running adversarial review against this session's own fixes.

## Next step
M2 Environment (PLAN.md) PR 2b: `core/util/random.hpp` (seeded `std::mt19937_64`, in-house
Box-Muller/uniform transforms) and `plugins/physical/wind/dryden` (MIL-F-8785C), on PR 2's
`begin_frame`/`wind_n` split -- real per-frame airspeed/altitude, so the PSD test drives a
synthetic, held-constant `VehicleState`. Reuses PR 2's bitwise determinism harness. Accept: Dryden
PSD matches MIL-F-8785C within statistical bounds at a fixed seed; two same-seed runs bitwise
identical on the gust sequence/registry snapshot, and across `reset()`. Opener:
`claude --worktree m2-environment-2b --permission-mode plan`

## Tried / failed
First response to "wind without earth" left it disclosed-only by analogy to another, correctly-
deferred check; wrong -- an unconditional null deref inside `step()`'s `noexcept`, not a
domain-validity boundary case. Caught by a second adversarial-review pass, fixed (`lessons.md`).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
