# ADR-0013 — The NESC check-case oracle is an ensemble band over human-placed data, derived offline in Python

Status: **accepted**, 2026-09-17. Elaborates `docs/process/workflow.md` §Verification ("golden-trajectory
regression with per-channel absolute + relative tolerances, stored as data") and ADR-0002 (SI inside the tree,
conversions at boundaries). Governs `PLAN.md`'s M2 PR 3.

## Context

NASA/TM-2015-218675's check-case data is ~82 MB across 17 atmospheric scenarios, 4-6 CSV trajectories per
scenario, one per participating simulation tool. It is human-placed under `docs/refs/` and gitignored
(`docs/refs/*`), so no test may read it directly: a test that parses those files cannot run on a fresh clone.

The report publishes no single reference trajectory and no per-case tolerance, by editorial choice — it presents
cross-tool comparison plots instead (Volume I §6.3, §7.3). The ensemble *is* the oracle.

Every column is US customary and inconsistently so: `gePosition_ft_*` (ECEF feet), `feVelocity_ft_s_*`,
`altitudeMsl_ft`, `latitude_deg`/`longitude_deg`, `bodyAngularRateWrtEi_deg_s_*` (ECI-referenced),
`altitudeRateWrtMsl_ft_min`, `airDensity_slug_ft3`, `ambientPressure_lbf_ft2`, `ambientTemperature_dgR`,
`aero_bodyForce_lbf_*`, `aero_bodyMoment_ftlbf_*`, `trueAirspeed_nmi_h` — four time bases and Rankine.

Measured facts about the grids and the spread, which shape the decision:

- Every case runs 0-30 s. Five sims sample at exactly 10 Hz (301 points, dt = 0.1 s uniform); `sim_05` alone
  samples at 100 Hz (3001 points, nominal 0.01 s with float noise).
- `sim_05`'s every-tenth sample lands on k*0.1 within 7.6e-7 s; `sim_06`'s own grid drifts 1.4e-11 s.
- Cases 1-3 and 6-10 publish 31 columns; cases 4 and 5 publish 27, a different header.
- Channel availability varies per sim, not only per case (`eulerAngle_deg_Pitch` has 5 contributors where
  `altitudeMsl_ft` has 6).
- Inter-tool spread is strongly case- and time-dependent: case 1 (dragless) reaches 2e-3 ft altitude spread at
  t=30 (1.3e-7 relative); case 7 (drag + steady wind) reaches 0.90 ft (5.5e-5 relative). Both grow monotonically.

## Decision

**A Python generator, run by hand, converts and reduces; its small output is committed and guarded.** Following
`tests/golden/atmosphere/generate_isa1976_table.py` exactly: the generator is human-owned, is not run by
`just check`, and what the gate consumes is its committed output under `tests/golden/nesc/`. The raw 82 MB stays
gitignored in `docs/refs/`.

**Unit conversion happens in the generator, not in `core/util/units.hpp`.** ADR-0002's "named function with a unit
test in core/util/units" governs conversions ASTRA's *runtime* performs at a config boundary. No ASTRA runtime code
ever sees `slug/ft3` or `dgR`; these units enter a data file, not a `Config`. Putting knowledge of one vendor's CSV
layout into `core/` would violate the placement rule `AGENTS.md` draws. The generator states each constant with its
citation, as the ISA generator does.

**The committed golden is the band, not the ensemble.** Per case, per channel, per sample time: the reference and
the tolerance, not 4-6 full trajectories. One small file per case.

    reference = mean across participating sims
    tol_abs   = k * max over t of (max - min across sims)
    tol_rel   = k * max over t of ((max - min) / |mean|), omitted where |mean| ~ 0
    pass      : |astra - reference| <= tol_abs + tol_rel * |reference|
    k         = 3

Both terms are required: `altitudeMsl_ft` is ~3e4 so a relative-only bound is meaningless; `latitude_deg` and
`eulerAngle_deg_Pitch` are identically zero in several cases so an absolute-only bound is meaningless. `k` is the
single judgment number and is stated once. The tolerance is derived from inter-tool disagreement and is **never**
fitted to ASTRA's own output; `k` may be tightened as cases prove out, never widened to clear a failure
(`AGENTS.md`: widening a tolerance is a blocking review finding).

**No interpolation. The common grid is 10 Hz, by exact selection.** The generator decimates `sim_05` by ten and
matches on the 10 Hz grid by nearest sample within 1e-6 s. ASTRA needs no rate change: ADR-0010 pins dt = 1 ms, so
0.1 s is exactly 100 frames and `t = frame * dt` compares equal to the sample instants as doubles (`100*0.001 ==
0.1`, `30000*0.001 == 30.0`), with no accumulated drift. The comparison is a scheduler divisor of 100, and the
harness samples AT those instants — it never decimates or averages ASTRA's own trajectory.

**The generator reports band width per channel per case.** A band wide enough to admit an obviously wrong answer is
not a test; such channels are recorded as non-discriminating rather than silently counted as passes.

## Rejected alternatives

- **Convert in C++ at test load time.** Requires ~11 new conversions in `core/util/units.hpp` that exist only to
  read one vendor's file format, and leaves the test depending on 82 MB of gitignored input, so it cannot run on a
  fresh clone or in CI. The reproducibility failure is decisive on its own.
- **Commit the raw ensemble.** Repeats the 3.4 MB PDF mistake twenty times over, and the test does not need six
  trajectories — it needs to know whether one trajectory lies in a band.
- **Post-process the comparison in Python against a dumped ASTRA trajectory.** There is no logger until M5
  (`docs/architecture.md` §16), so this needs a trajectory-dump path built for tests alone, and it moves the physics
  oracle out of the ctest where every other one lives.
- **Interpolate onto a common grid.** Unnecessary: the fine grid contains the coarse one to 7.6e-7 s. Interpolation
  would add a judgment (linear vs. spline) that changes the band, for no gain.
- **One global tolerance.** Case 1 and case 7 differ by three orders of magnitude in inter-tool spread. Any single
  number is either unreachable for one or vacuous for the other.
- **Strict min/max envelope (ASTRA must lie inside the hull).** Harsher than the tools' own agreement warrants; a
  correct implementation differing legitimately from every participant would fail. Retained as a possible tightening
  if `k = 3` proves vacuous.

## Consequences

- `tests/golden/nesc/` becomes a guarded golden tree alongside `tests/golden/{frames, atmosphere, wind}`; its
  generator lives beside it and is run by Joe, never by an agent, never by the gate.
- `docs/refs/` is established as the home for human-placed reference material (`docs/refs/README.md`), gitignored
  and cited by page. `AGENTS.md`'s "Read first" names it so a future session finds it.
- Case 1 is expected to be the hardest to match, not the easiest: it is nearly analytic, the tools agree to ~7
  significant figures, and matching that with fixed-step RK4 at 1 kHz against variable-step integrators is a real
  constraint. The time-grid noise alone (7.6e-7 s at 960 ft/s is ~7e-4 ft) is the same order as case 1's entire
  inter-tool spread, and the generator says so.
- A 10 Hz comparison leaves 99 of every 100 frames unchecked. Per-frame behavior stays covered by the
  `HealthMonitor` and no-allocation tests, not by this.
- ADR-0012's per-frame domain validity (PR 2c) must land first; cases 9 and 10 fly trajectories that move in
  latitude, and PR 3's drag `WrenchSource` is the first consumer that can read a NaN `AtmoState`.
