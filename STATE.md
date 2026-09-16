# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing once PR #25 (branch `worktree-m2-environment-2`) merges. Adds `WindModel`
(`begin_frame`/`wind_n`/`shareable_across_vehicles`, `docs/architecture.md` §7) and the per-vehicle
`EnvironmentSample` latch carrying wind (and, from PR 3, atmosphere) to a `WrenchSource`, plus
`plugins/physical/wind/uniform`. `physical.wind` is optional, like `physical.atmosphere`;
`Vehicle::init` rejects a `WrenchSource` requiring wind with none configured, and one requiring
atmosphere unconditionally (no PR before PR 3 connects the two). `assumed_position_representation()`
mirrors `GravityModel`'s mechanism; `from_config` rejects a mismatched Earth model. `kCoreApiVersion`
bumped 1 -> 3 (`WrenchSource`'s ABI break, then `WindModel`'s own settled vtable).
**Disclosed, not fixed** (Codex adversarial review, three rounds, verdict "approve"):
`assumed_position_representation()` is declarative, not enforced -- a plugin can declare `kAny`
while still reading `vs.p_n()` internally. Not new: `GravityModel`'s identical mechanism (M2 PR 1)
has had this property since it shipped.

## Last verified result
`just check` exited 0 (`check: OK`) on every preset, 177/177 tests each (final run, after four
Codex-review fixup rounds this session). Codex review: no findings. Codex adversarial review, three
rounds: round 1 found `begin_frame` exposed raw, representation-dependent `p_n` -- fixed with
`altitude_m`; round 2 found that declaration unenforced -- fixed with
`assumed_position_representation()`; round 3 found `kCoreApiVersion` needed a further bump (2 -> 3)
and confirmed the disclosure above -- verdict: approve.

## Next step
M2 Environment (PLAN.md) PR 2b: `core/util/random.hpp` (seeded `std::mt19937_64`, in-house
Box-Muller/uniform transforms) and `plugins/physical/wind/dryden` (MIL-F-8785C), on PR 2's
`begin_frame`/`wind_n` split -- real per-frame true airspeed/altitude, so the PSD test drives a
synthetic, held-constant `VehicleState`. Reuses PR 2's bitwise determinism harness against
`dryden`. Accept: Dryden PSD matches MIL-F-8785C within statistical bounds at a fixed seed; two
same-seed runs bitwise identical on the gust sequence and registry snapshot, and across `reset()`.
Opener: `claude --worktree m2-environment-2b --permission-mode plan`

## Tried / failed
Blocked on the wrong `just check` pid twice (a transient child, not the real process); recovered
via `pgrep -x just` per the existing pid-finding rule.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
