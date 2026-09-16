# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing once PR #25 (branch `worktree-m2-environment-2`) merges. Adds `WindModel`
(`begin_frame`/`wind_n`/`shareable_across_vehicles`, §7) and the per-vehicle `EnvironmentSample`
latch carrying wind (and, from PR 3, atmosphere) to a `WrenchSource`, plus
`plugins/physical/wind/uniform`. `physical.wind`/`physical.atmosphere` share one divisor check.
`Simulation::for_testing` (test-only) is the only way to exercise D1's "per vehicle" half of the
call contract and D4's shareability guard, since `from_config` never builds more than one vehicle
(ADR-0011). `kCoreApiVersion` bumped 1 -> 3.
**Disclosed, not fixed** (Codex + Cowork review): (1) `assumed_position_representation()` is
declarative, not enforced, same as `GravityModel`'s mechanism since M2 PR 1. (2) First per-frame
caller of `EarthModel::altitude()`; PR 1's/1b's domain-validity gap now covers it, re-accepted
knowingly -- PR 2b's `dryden` inherits it further. (3) `HealthMonitor` has no environment check, so
a NaN `wind_n` halts naming the *consuming* `WrenchSource`, not the wind plugin, same shape as
`AtmoState`'s gap; PR 3's drag model is the first thing that can carry one into the EOM.

## Last verified result
`just check` exited 0 (`check: OK`) on every preset. Codex review + adversarial review: no
findings, verdict approve. Separate Cowork review of the merged-ready branch: four findings, all
resolved -- shared divisor check + new atmosphere-divisor test; the two gaps above disclosed;
`Simulation::for_testing` added, its three multi-vehicle tests verified to fail under the exact
regression described, before being fixed; the single-vehicle test renamed to what it proves.

## Next step
M2 Environment (PLAN.md) PR 2b: `core/util/random.hpp` (seeded `std::mt19937_64`, in-house
Box-Muller/uniform transforms) and `plugins/physical/wind/dryden` (MIL-F-8785C), on PR 2's
`begin_frame`/`wind_n` split -- real per-frame true airspeed/altitude, so the PSD test drives a
synthetic, held-constant `VehicleState`. Reuses PR 2's bitwise determinism harness. Accept: Dryden
PSD matches MIL-F-8785C within statistical bounds at a fixed seed; two same-seed runs bitwise
identical on the gust sequence and registry snapshot, and across `reset()`.
Opener: `claude --worktree m2-environment-2b --permission-mode plan`

## Tried / failed
`Simulation::for_testing`'s first draft never wired physics, so `eom_.has_value()` silently
skipped every `WrenchSource`; caught by the multi-vehicle test itself failing, fixed by threading
a `GravityModel*`/mass `ModelSpec` through.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
