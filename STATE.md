# STATE (rewritten each session, never appended; ≤ 40 lines; written as true once the PR carrying it has merged)

## In flight
Nothing once PR #24 (branch `worktree-m2-environment-1b`) merges. Adds `AtmosphereModel`
(`AtmoState`, `AltitudeRange`) and `plugins/physical/atmosphere/isa1976` (0-86 km geometric
altitude, docs/architecture.md §7). `physical.atmosphere` is an optional config slot;
`Simulation::from_config` checks the initial altitude against the atmosphere's `valid_range()` at
init (§15), naming `physical.atmosphere`/`initial.state.p_n` on rejection. No
position-representation check: `at(double altitude_m)` never sees `p_n`.
**Disclosed, not fixed** (Codex adversarial review, four rounds -- round 4 stayed "no-ship" on this
point after every other finding was fixed): `earth/wgs84_rotating`'s `EarthModel::altitude()`
returns height above the WGS-84 ellipsoid, not the sea-level/geoid reference ISA-1976 assumes;
differs by the geoid undulation (~-105 to +85 m), bounding density error at ~1.3% (barometric
approximation, scale height ~8500 m). Needs a geoid model (EGM96/EGM2008), a separate
plan-reviewed PR (`PLAN.md`'s M2 PR 3 line). Also carries PR 1's per-frame Earth/gravity
domain-validity gap onto `physical.atmosphere` (same fix needed, not bolted on here).

## Last verified result
`just check` exited 0 (`check: OK`) on every preset, 152/152 tests each (four runs: implementation,
three Codex-review fixup rounds). Codex review: no findings. Codex adversarial review, four rounds:
round 1 found the WGS-84/ISA datum gap and an undisclosed per-frame atmosphere domain gap -- the
latter's disclosure added and confirmed; round 3 caught `docs/architecture.md` §7 overclaiming
`HealthMonitor` monitors `AtmoState` and omitting `valid_range()` -- fixed, confirmed round 4.
Round 4's remaining finding is the datum gap above -- carried forward as disclosed (Joe's call,
reasoning in `PLAN.md`'s PR 1b and PR 3 lines), not silently dropped.

## Next step
M2 Environment (PLAN.md) PR 2: `WindModel` (§7 signature) with `plugins/physical/wind/{uniform,
dryden}` (seeded `std::mt19937_64`, in-house transforms, seed logged). Accept: Dryden PSD matches
the MIL-F-8785C spec within statistical bounds at a fixed seed; steady-wind NESC cases within
tolerance; bit-identical logs across two same-seed runs.
Opener (model and effort pinned in `.claude/settings.json`; escalation is Joe's):
   `claude --worktree m2-environment-2 --permission-mode plan`

## Tried / failed
Double-backgrounded `just check` once (`&`/`disown` inside `run_in_background: true`); recovered per
`docs/lessons.md`'s existing rule by finding the real pid and blocking on it, not restarting.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
