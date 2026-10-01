# M2 PR 3 — NESC check-case harness

Recovered from `~/.claude/plans/m2-pr-3-nesc-hazy-fiddle.md` on 2026-09-30. Approval: the file records none; it recommends the 3a/3b split, 3a merged as #29, and 3b was replanned in `nesc-harness.md` (#31). Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31). Spelling normalized to American English.

## Context

`PLAN.md`'s M2 row requires "NASA check cases 1–10 within NESC tolerances". ADR-0013 settled the
oracle (ensemble band over the 4–6 participating sims, `k = 3`, generated offline in Python, never
fitted to ASTRA). ADR-0012 (per-frame domain validity) landed in PR #27. The data is now in
`docs/refs/`. What is missing is everything that turns a config into a comparable trajectory: a
drag/damping `WrenchSource` (M2's only consumer of wind and atmosphere), `EnvironmentSample::
atmosphere` actually being populated, a rotating spherical Earth (case 5), an altitude-keyed wind
(case 8), and the harness plus goldens themselves.

**Recommendation: split into PR 3a and PR 3b.** Reasoning at the end; everything the request asked
the plan to *state* is stated here for both halves, so this is a decision Joe can make on one read.

---

## 1. The wind injection point — already shipped; the PLAN line described a road not taken

`PLAN.md`'s PR 3 line says `WrenchSource::wrench_b` "needs its own `StepContext` latch … since
`wrench_b(vs, env)` itself carries none." **Corrected provenance** (I first called this sentence
stale, as if it predated M2 PR 2; `git log -S` shows it was introduced *by* #25 and only touched by
#27 — it isn't residue from before the latch existed). What it actually was: #25 weighing a
`tests/plugins/`-style *time-varying* wind `WrenchSource` design, which ADR-0013 later settled
differently (case 8's wind is altitude-keyed, so `plugins/physical/wind/table`, a `WindModel`, not
a bespoke `WrenchSource`-side latch). The conclusion below is unchanged; the reason for it is not
"the sentence predates the latch," it's "the latch already does this and a time-varying-latch
design was never built because ADR-0013 didn't need it." M2 PR 2 (#25) **is** the wind injection
point and already ran the `/codex:rescue` round `CLAUDE.md`'s M2 trigger list names:

- `core/include/astra/kernel/environment_sample.hpp` — `EnvironmentSample{wind_n, atmosphere}`.
- `core/src/kernel/simulation.cpp:491-544` — `Simulation::step` latches it per vehicle at
  frame-start state, before the wrench stage, using `earth_->altitude(vs)` as the one Earth-derived
  altitude; `vehicle.set_environment(sample)`.
- `core/include/astra/interfaces/wrench_source.hpp:28` — `wrench_b(const VehicleState&, const
  EnvironmentSample&) const noexcept`. The signature change cost `kCoreApiVersion` 1 → 2.

So the drag `WrenchSource` gets `wind_n` from `env.wind_n`. No new latch, no `StepContext`
parameter, no ABI change for wind. **PR 3's own new decision is the symmetric half — atmosphere**,
which is on the same trigger list ("how … atmosphere models get geodetic position from the Earth
model"). `/codex:rescue` reviewed exactly this merge in plan mode before any code was written;
verdict **"sound with listed changes"** (should-fix items folded into the bullets below, one
wording correction on the ABI justification, no correctness objection to the merge itself). Its
noted residual risks, not fixed because they are pre-existing and out of this PR's scope: a future
*stateful* atmosphere model would need a new lifecycle contract (`at()` is a const altitude-only
query by design, `atmosphere_model.hpp`); an out-of-range NaN is attributed to the consuming
`WrenchSource`, not the atmosphere plugin, the same shape as wind's own disclosed gap
(`environment_sample.hpp:18-27`).

```cpp
// Simulation::step, replacing the current `if (wind_ != nullptr)` block
EnvironmentSample sample;                       // wind zero / atmosphere all-NaN by default
if (wind_ != nullptr || atmosphere_ != nullptr) {
  const VehicleState vs{vehicle.state()};
  const double altitude_m = earth_->altitude(vs);          // ONE call, shared by both
  if (wind_ != nullptr) {
    wind_->begin_frame(ctx, vs, altitude_m);
    sample.wind_n = wind_->wind_n(ctx, vs);
  }
  if (atmosphere_ != nullptr) {
    sample.atmosphere = atmosphere_->at(altitude_m);        // never clamped; NaN out of range
  }
}
vehicle.set_environment(sample);
```

Pinned by this decision, stated not deferred:

- **One `earth_->altitude(vs)` per vehicle per frame**, at frame-start state, shared by wind and
  atmosphere. Not two calls, and not the post-step altitude ADR-0012's domain check uses
  (`simulation.cpp:527`) — that one is deliberately a different state and stays separate.
- **`atmosphere_` is sampled whether or not `physical.wind` is configured**, and vice versa; the
  two slots are independent. `earth_` is non-null whenever either is (both load only under
  `physical`, earth first).
- **Out of range stays all-NaN**, not clamped (`atmosphere_model.hpp:40`). ADR-0012's post-step
  `valid_range()` check already halts the run, so the NaN is a backstop, not the reporting path.
- **`Vehicle::EnvironmentAvailability` gains `bool atmosphere`**, set from `atmosphere != nullptr`,
  in the same commit — `vehicle.cpp`'s unconditional rejection of an atmosphere requirement becomes
  the conditional one naming `physical.atmosphere`, exactly as wind already is.
- **No `kCoreApiVersion` bump.** `EnvironmentSample`/`EnvironmentRequirements` layouts are unchanged
  (the `atmosphere` field already exists), and no `AtmosphereModel`/`WrenchSource` virtual is added
  or reordered — the version gate is a *plugin-facing ABI* contract (`core_api.hpp.in:8-21`), and
  nothing plugin-facing moves. (Codex plan review corrected the original justification here:
  `kernel/vehicle.hpp` **is** publicly include-visible via `core/CMakeLists.txt:24-29`, so
  "core-internal" was the wrong argument; "no plugin-facing ABI change" is the right one.) The only
  behavior change for an out-of-tree plugin is that an atmosphere requirement now *can* be
  satisfied — a loosening, not a break.
- **`Simulation::for_testing` does not gain an atmosphere parameter.** The drag source's own unit
  tests go through `from_config` with a real YAML, like the NESC cases do. Both `for_testing` call
  sites (`simulation.cpp:470-475`) therefore keep `atmosphere: false` deliberately.
- **One sentence needed in `simulation.hpp:68-74`'s own doc comment**, since this PR changes what
  that comment's claim actually protects against. Today it says `for_testing`'s missing atmosphere
  parameter merely means ADR-0012's post-step domain check is skipped (`atmosphere_ == nullptr`) —
  true and harmless on its own. After this PR, `Simulation::step`'s merged sampling block guards
  `earth_->altitude(vs)` on `wind_ != nullptr || atmosphere_ != nullptr`, inside `step()`'s own
  `noexcept` — exactly the shape `simulation.hpp:77-99` already documents getting wrong once for
  wind (a non-null `wind_` with a null `earth_` was UB before that fix). The same absence that
  currently only skips a check is now *also* the only thing preventing a future `for_testing`
  atmosphere parameter from reintroducing that bug for atmosphere. Add: "a future atmosphere
  parameter on `for_testing` needs the same earth-required `Error` wind already got, not a second,
  independent discovery of the same crash."
- **All four `EnvironmentAvailability` construction sites, named** (Codex plan review said three;
  a second pass over `simulation.cpp` found a fourth): `simulation.cpp:341` (the `physical`-present
  `from_config` path) becomes `{wind != nullptr, atmosphere != nullptr}`; `simulation.cpp:375` (the
  no-`physical` `from_config` path) is unchanged — correctly, since neither slot can exist there;
  `simulation.cpp:471` and `:474` (`for_testing`'s two branches) both keep atmosphere false, per the
  bullet above. `Vehicle::init` (`vehicle.cpp:30-44`) has no other availability path. Naming all
  four here rather than three costs one line and heads off exactly the kind of
  from_config/for_testing divergence-by-omission PLAN.md's PR 2 line already records catching three
  separate times (wind's divisor guard without atmosphere's; wind scheduled without earth/gravity;
  `for_testing`'s own validation-vs-`Config` gap).
- **`tests/unit/wind/wind_test.cpp:353-374` must be replaced, not extended** (Codex plan review,
  should-fix): it currently asserts that a *configured* atmosphere requirement is rejected, which is
  exactly the behavior this PR removes. It becomes two tests: configured atmosphere is accepted;
  absent atmosphere is still rejected, naming `physical.atmosphere`.
- **A dedicated atmosphere-only latching test** (Codex plan review, should-fix), not just coverage
  via the drag source: today `step()` samples atmosphere only inside the `wind_ != nullptr` branch
  (`simulation.cpp:501-510`); the merged block must be proven to sample it with `physical.wind`
  absent too. `tests/plugins/atmosphere_consumer/` already exists for exactly this
  (`atmosphere_consumer.cpp:23-33`) — extend it to assert finite fields when configured and, in a
  second case, unclamped NaN when the vehicle starts outside `valid_range()`.

## 2. The geoid gap — it does not exist in this data set, verified two ways

`PLAN.md` and `atmosphere_model.hpp` carry a disclosed gap: `wgs84_rotating::altitude()` returns
height above the **ellipsoid**, while the NESC column is named `altitudeMsl_ft`. Both sources say
they are the same thing here:

1. **The report's own definition** (Vol II, Appendix C.1 preamble, doc p.56): "Position is the
   initial position of the CM … latitude, longitude and **height above the reference ellipsoid or
   sphere**. Units are deg / deg / ft above reference ellipsoid or sphere." The column label is
   "MSL"; the definition is ellipsoidal.
2. **Measured, this session**, over every sim that publishes ECEF (`gePosition_ft_*`), every sample
   of every case: invert ECEF → geodetic with the exact Ferrari/Bowring form and difference the
   resulting `h` against that row's own `altitudeMsl_ft`.

| cases | sims with ECEF | max \|h_ellipsoidal − altitudeMsl\| over all 301 samples |
|---|---|---|
| 1, 2, 3, 6, 7, 8, 9 | sim_01, sim_03, sim_05, sim_06 | **≤ 6.1e-5 m** (sim_01; the others ≤ 1e-9 m) |
| 10 | same | ≤ 0.025 m (sim_06 at t = 30 only; others ≤ 6.1e-5 m) |
| 4, 5 | sim_05, sim_06 | −7129.8191 m — exactly `6371007.1809 − 6378137.0`, i.e. height above the **sphere** (Vol II Table 73's "Radius of the sphere Earth model", confirming those two cases are spherical, not ellipsoidal) |

**Conclusion: no geoid model is needed, for any of the ten cases, and none should be added.** The
undulation ASTRA would have to apply is 0 m. `wgs84_rotating::altitude()` feeding `isa1976`
unchanged is exactly what the ensemble did.

The request asked for the per-case margin anyway. Drag is the only path density reaches the
trajectory, so to first order a relative density error `δ` moves any channel by `δ ×` the
drag-driven part of that channel, which the data itself supplies (each drag case's altitude at
t = 30 minus its dragless twin's). Scale height `H = R·T/g` at each case's mid-fall altitude:

| case | Earth | alt band (k=3) | drag-driven Δalt | H | dev @ N = 105 m | **break-even N** |
|---|---|---|---|---|---|---|
| 1, 2, 3 | WGS-84 | 1.9, 1.9, 0.73 mm | 0 (C_D = 0) | — | **0** | infinite |
| 4 | sphere | 16.56 m | 194.1 m | 7106 m | 2.87 m | 606 m (n/a: sphere) |
| 5 | sphere | 16.61 m | 207.9 m | 7105 m | 3.07 m | 568 m (n/a: sphere) |
| 6 | WGS-84 | 0.819 m | 208.9 m | 7105 m | 3.09 m ✗ | **27.9 m** |
| 7 | WGS-84 | 0.820 m | 209.2 m | 7104 m | 3.09 m ✗ | **27.8 m** |
| 8 | WGS-84 | 0.818 m | 210.9 m | 7104 m | 3.12 m ✗ | **27.5 m** |
| 9 | WGS-84 | 3.90 m | 1646 m | 8154 m | 21.2 m ✗ | **19.3 m** |
| 10 | WGS-84 | 3.89 m | 1660 m | 8155 m | 21.4 m ✗ | **19.1 m** |

So: cases 1–3 are immune at any undulation (no drag at all — the PLAN line already said case 1 is;
cases 2 and 3 are too, their C_D is zero and only the damping moments are aerodynamic). Cases 4 and
5 are spherical-Earth, so the concept does not apply, and would survive 568 m anyway. Cases 6–10
would fail the worst-case 105 m bound (`~1.3–1.6%` density) by 3–5×, and their break-even
undulations (19–28 m) are the same order as a real equatorial undulation — which is precisely why
it matters that the measured requirement is 0 m and not "small". **If a future case is ever placed
somewhere with a real MSL reference, this becomes its own plan-reviewed PR with a geoid model; it
is not one here.** The disclosed gap in `atmosphere_model.hpp` and `PLAN.md` stays disclosed
(it is still true of `wgs84_rotating` in general), with a pointer to this measurement.

**A green case 4/5 will be nearly vacuous evidence, and the PR body must say so, not just this
plan.** **Corrected citation** (I first mis-attributed Table 78's per-signal numbers to Table 77's
per-case ones — they're different tables, and I also swapped which participant had which figure).
**Table 77** (per-case, worst disagreement across *any* signal, for that sim, in that case) is the
right source for a per-case claim: case 4's row reads SIM 2 = 9.998%, SIM 4/5/6 = 3.333%; case 5's
row reads SIM 2 = 10.011%, SIM 4/5/6 = 3.337% — both cases' own largest-difference column tops out
just under 10%, an order of magnitude above case 6's own 0.142% (same table, same row shape,
WGS-84 instead of the round sphere). **Table 78** (per-signal, largest value across *all ten cases
1–10 combined*, not case-specific) separately shows `aero bodyForce lbf {X,Y,Z}` topping out at
3.999/10.011/3.337% for SIM 2 and 1.333/3.337/1.112% for SIM 4 — consistent with, but not
independent confirmation of, Table 77's per-case figures, since Table 78 doesn't say which case(s)
produced its own maxima. Footnote b excludes only SIM 2's X-force in cases 4/5 (its Y and Z, and
its 9.998%/10.011% per-case maxima above, stay in the band). Whichever table is cited, the
conclusion is the same: cases 4/5's own ~10% worst-case spread dwarfs case 6's 0.14%, so ASTRA
passing case 4/5 within tolerance mostly confirms the non-rotating-sphere geometry and the
inverse-square/J2 gravity split, not the drag model to any useful precision. Cases 6–10 (WGS-84,
tighter bands) are where drag actually gets tested.

## 3. `tests/golden/nesc/` — what Joe places, and when

Guarded by `.claude/hooks/guard-oracle.sh` (`tests/golden/` and any path containing `tolerance`).
All of this lands in **PR 3b**.

**Correction to how I first described the guard window.** `generate_nesc_band.py` itself lives
under `tests/golden/nesc/` — a path the guard blocks on `Write` and on any `Bash` command that
targets it, not just on the final commit. That means the lift has to come **first**, not after I've
already written the generator, and it stays lifted (the marker persists for the whole session, per
`guard-oracle.sh`'s own comment) through every guarded edit PR 3b touches: the generator itself,
its committed output, the ISA generator's geopotential-check edit (§4), the new ADR (below), and
the `case1_dragless_sphere.hpp` / `nesc_case1_test.cpp` deletions. That is **not** "lifted exactly
once, at one point in the session" as I first wrote — it is armed for the session's entire PR 3b
portion. Saying that plainly, since it's what Joe is actually approving when he touches the marker.

```
tests/golden/nesc/
  generate_nesc_band.py            # human-owned generator, never run by `just check`
  case01_dragless_sphere.hpp       # ~60 lines: per-channel tol_abs/tol_rel + flags. JOE READS THIS.
  case01_dragless_sphere_ref.csv   # 301 x nChannels reference means. Machine data.
  ... case02 .. case10 (same pair)
```

- **Why a `.hpp` + `.csv` pair and not one header.** The references are 66,220 doubles across the
  ten cases (301 samples x ~22 channels x 10), ~1.2 MB. That is too much to compile into four
  presets and far too much for Joe to read. The tolerances are ~220 numbers — that is the part
  that is a judgment and the part Joe reads, so it goes in the header, alone. Both halves are under
  `tests/golden/` and so both are guarded. The test reads the CSV at init (not on a per-frame path)
  through a new `ASTRA_TEST_GOLDEN_DIR` compile definition (`tests/golden/nesc`).
- **`ASTRA_TEST_GOLDEN_DIR` is not the same pattern as `ASTRA_TEST_CONFIG_DIR`** — I said it was;
  it isn't. `ASTRA_TEST_CONFIG_DIR` is `tests/configs`, but PLAN.md's own line puts these YAMLs
  under `tests/physics/nesc/`, a different directory. `nesc_cases_test.cpp` needs **two** new
  compile defs, both added to `tests/physics/CMakeLists.txt`: `ASTRA_TEST_NESC_CONFIG_DIR`
  (`tests/physics/nesc`, ungoverned by the guard) and `ASTRA_TEST_GOLDEN_DIR` (`tests/golden/nesc`,
  guarded, as above).
- **Reading the raw CSVs, not just the reference/tolerance pair, is the generator's problem and it
  has real format quirks, measured this session:**
  - Column count varies **by sim file, not by case** (ADR-0013's own "cases 4 and 5 publish 27"
    describes two specific participants' files, not every file in those cases) — observed counts
    across the ten cases' 54 files are 22, 27, 31, 32, 37, 38. The generator must key every column
    by its header name, per file, never by position or by an assumed case-wide column count.
  - Every `sim_05` file duplicates the `feVelocity_ft_s_Z` header (columns 13 and 38) — values are
    identical in all ten cases where checked, and Python's `csv.DictReader` silently keeps the
    last occurrence, which happens to be harmless here. The generator asserts the two columns are
    equal before dropping one, so a future case where they *aren't* equal fails loudly instead of
    silently picking one arbitrarily.
- **Reference precision.** Each value is printed to the precision its own tolerance makes
  meaningful (rounding ≤ 1e-3 of `tol_abs`), never fewer than 6 significant digits — so the file is
  readable rather than 17-digit noise, and the rounding can never move a pass/fail.
- **`case1_dragless_sphere.hpp` is absorbed and deleted**, with `tests/physics/nesc_case1_test.cpp`
  (PLAN.md: "absorbing PR 1's hand-written case 1 into the harness"). Both are guarded deletions.
  Worth keeping in the commit message: that file's independently integrated `longitude(30 s) =
  1.0027827850208136e-06` rad agrees with NESC `sim_05`'s own value to **8.5e-15 rad** — the
  ensemble independently confirms the oracle PR 1 built by hand.

### The tolerances I am proposing, and the second judgment number ADR-0013 doesn't have

ADR-0013 pins `reference = mean`, `tol_abs = k·max_t(spread)`, `tol_rel = k·max_t(spread/|mean|)`,
and says `k` is *the* single judgment number, stated once. Applying the formula verbatim to the
real data surfaces a pass criterion the ADR's formula genuinely does not contain — not a comment to
drop into the generator, but a second number that needs its own record:

**Degenerate-zero channels.** Many channels are *analytically* zero for their case (case 1's
latitude, pitch, yaw, every angular rate; every aero force in cases 1–3). Every tool computes them
as exactly or nearly 0.0, so `max_t(spread)` is the ensemble's own roundoff and `tol_abs` comes out
at, e.g., **1.27e-19 rad** for case 1's latitude. That is not a physics tolerance, it is a
floating-point accident, and it would make a correct ASTRA fail on a channel nobody is testing.

I propose a **per-channel-kind resolution floor**, stated once, applied as
`tol_abs = max(k·max_t(spread), floor)`, chosen to sit far below any physically meaningful
difference and far above double-precision accumulation over 30,000 frames:

| channel kind | floor | why |
|---|---|---|
| angle (lat, lon, Euler) | 1e-12 rad | ≈ 6 µm on the Earth's surface |
| length (altitude) | 1e-9 m | 1 nm |
| velocity | 1e-9 m/s | |
| angular rate | 1e-12 rad/s | ≈ 1e-6 rad over 30 s |
| force / moment | 1e-12 N / N·m | ≈ 1e-13 of the largest force in the set |
| density / pressure / temperature / speed of sound | 1e-12 × the channel's own max \|reference\| | relative, since these span 5 orders of magnitude |

This floor only ever *loosens* a channel whose ensemble already agrees below the floor; it can
never loosen a channel that discriminates anything — **checked, not asserted**: every degenerate
channel in this data set has `max|reference|` at or below 1.1e-16 SI (case 1's latitude band is
1.27e-19 on a reference of 7.0e-21; case 10's pitch rate band is 3.7e-38 on a reference of
2.4e-39), while the *tightest genuinely non-zero* channel found — case 9's yaw, reference 1.571
rad — has a band of 2.1e-11 rad, 20× above the proposed 1e-12 floor. So on this data the floor
never touches a channel that discriminates anything; it only fires where the ensemble's own
agreement is already floating-point roundoff.

**This is Joe's decision, and it belongs in a new ADR, not a generator comment** — ADR-0013 states
plainly that `k` is the only judgment number, and the floor is a second one the formula as written
does not have. I propose **ADR-0014, elaborating ADR-0013**: the floor table below, its
derivation, and the "checked, not asserted" argument above, written up the way ADR-0013 itself
states a decision with its own rejected alternatives (the alternative here being: drop
degenerate-zero channels from the comparison entirely, which loses a real assertion — "latitude
stays exactly zero" is worth keeping — for cases 1–3 and case 9's own zero channels). Being a new
file under `docs/decisions/`, this is a guarded write too, folded into the same guard-lift window
as PR 3b's goldens (the corrected sequence below covers it).

The generator also emits, per ADR-0013, a **band-width report** naming any channel whose band is
wide enough to be non-discriminating (e.g. case 4/5 `Fy`, `tol_rel ≈ 70`, because one participant
disagrees by 10% — Vol II Table 77's own largest-difference column says the same).

**The report's own exclusions are honored**, with citations, since a band inflated by a channel the
report itself calls erroneous is exactly the "wide enough to admit an obviously wrong answer" case
ADR-0013 rejects (Vol II Tables 77/78 footnotes a–d): SIM 2's Euler angles (all cases), SIM 2's
aero moments (all cases), SIM 2's X-body force in cases 4–5, SIM 3's Y-body force in cases 6–8.

### Sequence for the guard, corrected

1. **Joe runs `touch .claude/touch-goldens` (in this tree or the main checkout) first** — before
   any file under `tests/golden/` or `docs/decisions/` (ADR-0014) can be written at all, since
   `generate_nesc_band.py` itself lives under the guarded `tests/golden/nesc/`.
2. I write and run the generator, committing its output under `tests/golden/nesc/`; draft ADR-0014;
   make the ISA generator's geopotential-check edit (§4); delete `case1_dragless_sphere.hpp` and
   `nesc_case1_test.cpp`. All of this happens in the one armed window, not four separate lifts.
3. Joe reads the ten tolerance headers, the band-width/exclusion report, and ADR-0014; the guard
   re-arms at next session start regardless of what he decides.
4. `just check`, Codex review, adversarial review, PR.

**Practical gotcha worth a `docs/lessons.md` entry:** `docs/refs/` is gitignored, so it does **not**
exist inside a worktree — it lives only in the main checkout. The generator takes `--refs <dir>`,
defaulting to `docs/refs/Atmospheric_checkcases`, and in a worktree session must be pointed at
`/home/joe/repos/ASTRA/docs/refs/Atmospheric_checkcases`.

### `docs/refs/README.md` — lines to add (PR 3a, unguarded)

```markdown
- **NASA/TM-2015-218675 Vol I** (`NASA-TM-2015-218675-Vol-I.pdf`) — NTRS. The assessment report;
  Table 6.3-1 (doc p.15) is the ten sphere-and-brick atmospheric check cases. Cited by ADR-0013 and
  `tests/golden/nesc/generate_nesc_band.py`.
- **NASA/TM-2015-218675 Vol II** (`NASA-TM-2015-218675-Vol-II.pdf`) — NTRS. Appendices. Cited by
  page: B.1.1/Tables 1-2 (sphere mass and aero, doc p.12), B.1.3/Tables 4-5 (brick, doc p.13),
  C.1.1-C.1.10/Tables 25-34 (per-case initial conditions, doc p.56-61), Table 73 (geodetic
  constants and unit conversions, doc p.93), Tables 77-78 (the participants' own largest
  differences and the exclusions the generator honors, doc p.591-592). Cited by
  `tests/golden/nesc/generate_nesc_band.py` and every `tests/physics/nesc/case*.yaml`.
- **NESC atmospheric check-case trajectories** (`Atmospheric_checkcases/`) — NTRS, the report's
  companion data: 17 scenarios, 4-6 CSV trajectories each, one per participating simulation tool,
  ~82 MB. Read only by `tests/golden/nesc/generate_nesc_band.py`, by hand, never by `just check`
  (ADR-0013). Note: gitignored files do not exist inside a git worktree - pass `--refs` pointing at
  the main checkout.
- **NESC initial conditions workbook** (`Initial_Conditions.xlsx`) — NTRS. A working spreadsheet;
  its constants are rounded back-conversions (Re = 20925646.99508 ft, omega = 7.292113023868e-5
  rad/s) and disagree with Vol II Table 73 in the 8th digit. **Table 73 is the authority**; the
  workbook is cited nowhere in the tree.
```

## 4. The transcribed-vs-fresh geopotential check (0.5 m)

`tests/golden/atmosphere/generate_isa1976_table.py` already stores each transcribed row's printed
geopotential `H` (`TranscribedRow.geopotential_h_m`) and comments that it is "recorded for the
reader" — nothing ever checks it. PR 3 adds the check the PLAN line names, in
`check_transcribed_row_tolerances()`:

```python
residual = geopotential_from_geometric(row.altitude_m) - row.geopotential_h_m
assert abs(residual) <= half_ulp(0)   # the printed H column is rounded to 1 m
```

**0.5 m is not a new judgment number**: it is `half_ulp(0)`, the file's own existing idiom, because
the document prints `H` to whole meters. Measured this session against the eight transcribed rows:

| Z (m) | H printed | H fresh | residual |
|---|---|---|---|
| 0 | 0 | 0.0000 | +0.0000 |
| 11000 | 10981 | 10980.9980 | −0.0020 |
| 20000 | 19937 | 19937.2723 | **+0.2723** |
| 32000 | 31840 | 31839.7187 | **−0.2813** |
| 47000 | 46655 | 46655.0467 | +0.0467 |
| 51000 | 50594 | 50594.0863 | +0.0863 |
| 71000 | 70216 | 70215.7462 | −0.2538 |
| 86000 | 84852 | 84852.0458 | +0.0458 |

Worst residual 0.281 m, inside 0.5 m with 1.8× margin. A geometric/geopotential column swap moves
the seven non-zero rows by 19–1148 m, so the check discriminates the error it exists for on those
rows — at `Z = 0`, `H = Z = 0` identically and a swap moves nothing, so that row's own assertion is
a no-op by construction, not a weaker check. This is a generator-time assertion only (the generator
is human-owned and not run by the gate), so it goes in during the same guard-lift window as PR 3b's
own goldens (see the corrected sequence below — it is not a separately-timed lift).

---

## The split

**PR 3a — the consumers** (~850 lines, no goldens, no guard, no NESC data, `just check` proves all
of it):

| file | change |
|---|---|
| `core/src/kernel/simulation.cpp` | populate `EnvironmentSample::atmosphere` (§1 above) |
| `core/include/astra/kernel/simulation.hpp` | one added sentence on `for_testing`'s doc comment (§1's last bullet) |
| `core/include/astra/kernel/vehicle.hpp`, `core/src/kernel/vehicle.cpp` | `EnvironmentAvailability::atmosphere`; conditional rejection naming `physical.atmosphere` |
| `core/include/astra/kernel/environment_sample.hpp`, `interfaces/atmosphere_model.hpp` | replace the "PR 3 is the first to write here" comments with what actually happened; keep the NaN-naming gap disclosed |
| `plugins/physical/earth/round/round.cpp` | `rotating: <bool>` (default false): non-zero `omega_ie_n = (ω cos φ, 0, −ω sin φ)` from `geodesy::kOmegaIe`, matching `wgs84_rotating.cpp:70`. Case 5 needs it; ADR-0003's `p_n` meaning is unchanged |
| `plugins/physical/wind/table/` | new plugin: piecewise-linear wind vs. altitude from config (`points: [{altitude, north, east, down}, …]`), `shareable_across_vehicles() == true`, `assumed_position_representation() == kAny` (it reads `begin_frame`'s Earth-derived `altitude_m`, never `p_n`). Case 8's profile is `V = 0.003h − 20` ft/s from the west — **node placement corrected below** |
| `tests/plugins/nesc_aero/` | the `WrenchSource`: constant-`C_D` sphere drag and brick rate damping, config-driven (`s`, `cd`, `b`, `cbar`, `clp`, `cmq`, `cnr`, all defaulting to 0, plus **`requires_wind` (default false)** — see the blocker fix below), `requires_environment() = {wind: requires_wind, atmosphere: true}`, publishing `force_b_{x,y,z}` / `moment_b_{l,m,n}` signals so the harness can compare them |
| `tests/unit/...` | atmosphere reaches a `WrenchSource`; the rejection now names `physical.atmosphere`; `round` rotating vs. not; `wind/table` interpolation, clamping, out-of-order/duplicate rejection; the drag/damping force at hand-checked conditions |

Physics the unit tests pin, each verified against the NESC data this session so PR 3a is not
guessing what PR 3b will need:

- **Sphere drag**: `F_b = −C_D · q̄ · S · v̂_air,b`, with `v_air = v_b − C_b^n · wind_n`.
  Case 7, **`sim_03`** at t = 0: ρ = 8.906861e-4 slug/ft³, V = 20 ft/s, S = 0.1963495 ft², C_D = 0.1
  → `F_y = 3.4977e-3 lbf`; the published value is `3.4977165569211e-3`. Named deliberately: `sim_01`
  publishes ρ = 8.913002e-4 at the same instant, 6.9e-4 relative higher — the participants disagree
  on t = 0 density by that much, so this identity is a `sim_03`-specific check, not a case-wide one.
- **Brick damping**: `L = ¼ ρ V S b² C_lp p`, `M = ¼ ρ V S c̄² C_mq q`, `N = ¼ ρ V S b² C_nr r`
  — **with `p,q,r` the inertial-referenced body rates ASTRA already stores**, not Earth-relative
  ones. Checked against case 3 `sim_04` at t = 10 s and t = 30 s: ratios 1.000030 / 1.000000 /
  1.000030, the residual being that participant's use of exact fractions (2/9 ft², 1/3 ft, 2/3 ft)
  where Vol II Table 5 prints 0.22222 / 0.33333 / 0.66667. The linear-in-V form has no 0/0 at
  V = 0, so no special case is needed at t = 0.

**BLOCKER, caught before writing any code: `nesc_aero` cannot declare `requires_environment().wind
= true` unconditionally.** Only cases 7 and 8 configure `physical.wind`; cases 1–6, 9, and 10 do
not. `Vehicle::init` (`vehicle.cpp:24-36`) calls `wrench_source->requires_environment()` and throws
`ConfigError` naming `physical.wind` whenever `requirements.wind && !availability.wind` — so a
`nesc_aero` that always requires wind would fail every one of those eight cases at load, before any
physics runs. **Fix: make the wind requirement config-driven.** `vehicle.cpp:24-31` calls
`model->init(spec.config, registry)` *before* `requires_environment()` is ever queried, so the
plugin has its own config in hand by then — `nesc_aero` reads a `requires_wind` key (default
`false`) in `init()`, stores it, and `requires_environment()` returns `{wind: requires_wind_,
atmosphere: true}`. The ten configs set `requires_wind: true` only for cases 7 and 8. **Two things
this must not become instead:** declaring `wind: false` and relying on `EnvironmentSample::wind_n`'s
zero default removes the init-time guard exactly where PLAN.md wants it (a config that means to
give the sphere wind but omits `physical.wind` would silently fly still air instead of failing
loudly); and bolting a zero-valued `plugins/physical/wind/uniform` onto the eight windless cases
just to satisfy an unconditional `requires_environment().wind = true` would run NESC's own dragless
comparison against a `physical.wind` slot NESC never configured. **PR 3a test:** one case with
`requires_wind: true` and `physical.wind` present (accepts), one with `requires_wind: true` and
`physical.wind` absent (rejects, naming `physical.wind`), one with `requires_wind: false` (default)
and no `physical.wind` (accepts) — the three states the actual ten configs exercise.

**PR 3b — the harness, the goldens, the ten cases** (~900 lines + generated data, one guard-lift
window covering every guarded edit below, not four separate lifts — see the corrected sequence):

| file | change |
|---|---|
| `docs/decisions/0014-*.md` | new ADR, elaborating ADR-0013: the per-channel-kind resolution floor and its "checked, not asserted" safety argument |
| `tests/golden/nesc/generate_nesc_band.py` | ADR-0013's reduction; SI conversion with Vol II Table 73's exact forms; 10 Hz grid by nearest-sample selection within 1e-6 s (sim_05 decimated ×10); per-file header-name column keying and the sim_05 duplicate-column assert; Tables 77/78 exclusions plus the t=0 airspeed-channel exclusion (cases 7/8); ADR-0014's floor; band-width/exclusion report |
| `tests/golden/nesc/case{01..10}_*.{hpp,csv}` | its output |
| `tests/golden/atmosphere/generate_isa1976_table.py` | §4's geopotential assertion |
| `tests/physics/nesc/case{01..10}_*.yaml` | the ten configs |
| `tests/physics/nesc/nesc_cases_test.cpp` | one `TEST_P` over the ten cases, with the state-then-`run(1)`-then-wrench-then-`run(99)` sampling above, not a plain `sim.run(100)` |
| `tests/physics/CMakeLists.txt` | `ASTRA_TEST_NESC_CONFIG_DIR` and `ASTRA_TEST_GOLDEN_DIR` compile defs |
| deletions | `tests/physics/nesc_case1_test.cpp`, `tests/golden/nesc/case1_dragless_sphere.hpp`, `tests/configs/nesc_case1_dragless_sphere.yaml` |
| `PLAN.md`, `STATE.md`, `docs/lessons.md` | tick, rewrite, worktree/`docs/refs` gotcha |

Channel extraction, all from existing public API: `p_n()` → lat/lon/alt; `rotate_b_to_n(q_nb, v_b)`
→ NED velocity (matches `feVelocity_ft_s_*`, confirmed against case 9's published initial
`[0, 1000, −1000]` ft/s); `euler_from_quaternion(q_nb)`; `omega_b()` (already inertial-referenced,
matching `bodyAngularRateWrtEi_deg_s_*`); `vehicle.environment().atmosphere`;
`sim.registry().find("v0.nesc_aero.force_b_x")` and friends.

**Correction: force/moment channels are one frame (1 ms) stale under a plain `sim.run(100)`, and
this was not in my first draft.** `Vehicle::integrate` (`vehicle.cpp:96-145`) calls `wrench_b(vs,
environment_)` once per frame, against the **pre-step** state `vs`, before the integrator advances
`state_`. The `nesc_aero` `WrenchSource`'s published `force_b_*`/`moment_b_*` signals therefore
hold the value computed from the state at the *start* of the last frame `run()` executed — after
`sim.run(100)`, the state is at `t = j·0.1`, but the force/moment signals still hold what was
computed from the state at `t = j·0.1 − 0.001`.

Measured against the actual `k = 3` band width (not just the raw rate of change, which my own
quick finite-difference proxy on the reference data underestimated): **worst 3.4% of the band**
(case 3's moment N), **2.2–2.5%** for cases 9/10's forces (fast-changing near peak dynamic
pressure), ≤1% everywhere else. Use these figures, not a hedged "order 1%" — they're what justifies
treating this as a real fix rather than a nice-to-have: **a 3.4% unstated bite out of `k = 3`'s
margin**, and ADR-0013 reserves *tightening* `k` as cases prove out — silently eating margin here
works against that. **Fix, no design change, one extra frame per sample:**

```cpp
// per sample j (0-based):
read_state(sim, j);          // state is fresh, at t = j * 0.1
sim.run(1);                  // this ONE frame's wrench_b(vs, env) uses the state just read (t = j*0.1)
read_wrench(sim, j);         // now matches the state just read, not the previous frame's
if (j < 300) sim.run(99);    // advance to t = (j+1) * 0.1 for the next sample's state read;
                              // skipped after the last sample -- nothing to advance toward
```
This is **30,001 frames total, not 30,000** (301 samples × 1 read-fresh-wrench frame, plus 300 ×
99 advance-to-next-sample frames — the trailing `run(99)` after sample 300 is skipped, but sample
300's own `run(1)` still runs, landing the sim one frame past `t = 30.0` at the very end). Harmless
— nothing in the NESC scenario or ASTRA's own health checks requires the run to stop dead at
exactly frame 30,000, and the extra frame's own state/wrench are read from nowhere (nothing reads
past sample 300). Simpler than "100 frames per sample either way," which undercounts by one.

**Second correction: `sim_06` (and, in case 8, `sim_03` too) publish zero for every
airspeed-derived channel at exactly `t = 0` in the wind cases, and the naive band formula turns
that into a badly wrong tolerance.** Measured: in cases 7 and 8, `sim_06`'s row at `t = 0` has
`trueAirspeed_nmi_h = 0`, `mach = 0`, `dynamicPressure_lbf_ft2 = 0`, and `aero_bodyForce_lbf_Y = 0`
(every other sim and every other instant show nonzero airspeed — the sphere starts with zero
*ground*-relative velocity but nonzero *air*-relative velocity in wind cases, so this is those two
sims not yet applying the wind at their own first sample, not a physical zero); case 8 additionally
has `sim_03`'s `Fy = 0` at `t = 0`. Because `tol_abs = k · max_t(spread)` takes the worst instant
over the whole 30 s run, this single bad sample sets the tolerance for **every** sample in the case:
case 8's `Fy` band goes from 0.0758 to 0.1286 lbf (1.7×), its TAS band from 0.074 to 124 (1677×),
its mach band from 0.00018 to 0.211 — and worse, case 7's `Fy` *reference* at `t = 0` becomes
`mean(5 × 0.0035, 0) = 0.00292` against every good sim's `0.0035`, a reference that is **17% wrong**
at the one instant a hand-derived closed form (§ above, "Sphere drag") is meant to check it against.
None of Vol II's own Table 77/78 exclusions cover this — it is a per-instant defect, not a
per-sim/per-case one. **Decision, stated here rather than left to the generator to discover quietly:
exclude `sim_06` (and, in case 8, `sim_03`) from every airspeed-derived channel — TAS, mach,
dynamic pressure, and the aero force/moment components — at `t = 0` only**, in both cases 7 and 8.
**Only `Fy` (`aero_bodyForce_lbf_Y`) is an actually-compared channel** (the drag `WrenchSource`
publishes `force_b_y`, which the harness checks against this reference); TAS, mach, and dynamic
pressure are not in the "Channel extraction" list above at all — they matter here only because the
generator's own band-width report (ADR-0013's own requirement, informational) would otherwise
print nonsense multipliers for them, not because ASTRA's trajectory is checked against them. The
generator asserts the exclusion covers exactly one sample per affected sim per case (so a future
data refresh that fixes it, or breaks it more broadly, is caught rather than silently masked
forever) and reports it next to the band-width report.

The ten configs, settled (Vol II Tables 25–34 + Table 73; the CSVs are authoritative where the
tables disagree — Tables 26/27 print position `[0,0,0]` for cases 2 and 3 while the data and the
prose both say 30,000 ft):

| case | earth | gravity | atmosphere | wind | `requires_wind` | C_D | damping | initial |
|---|---|---|---|---|---|---|---|---|
| 1 | wgs84_rotating | wgs84_j2 | isa1976 | — | false | 0 | — | 30,000 ft, v_b = 0, ω_b = 0 |
| 2 | wgs84_rotating | wgs84_j2 | isa1976 | — | false | 0 | 0 | brick, ω_b = [10, 20, 30] °/s |
| 3 | wgs84_rotating | wgs84_j2 | isa1976 | — | false | 0 | −1, −1, −1 | as case 2 |
| 4 | round (R = 6371007.1809, non-rotating) | inverse_square (`radius` = same R, `g0` = 9.820228349842926) | isa1976 | — | false | 0.1 | — | ω_b = [10, 20, 30] °/s |
| 5 | round (same R, **rotating: true**) | inverse_square (same) | isa1976 | — | false | 0.1 | — | ω_b = [10, 20, 30] °/s |
| 6 | wgs84_rotating | wgs84_j2 | isa1976 | — | false | 0.1 | — | as case 1 |
| 7 | wgs84_rotating | wgs84_j2 | isa1976 | uniform, 20 ft/s from west | **true** | 0.1 | — | as case 1 |
| 8 | wgs84_rotating | wgs84_j2 | isa1976 | **table**, see the corrected node placement below | **true** | 0.1 | — | as case 1 |
| 9 | wgs84_rotating | wgs84_j2 | isa1976 | — | false | 0.1 | — | sea level, v_n = [0, 1000, −1000] ft/s, ψ = 90° |
| 10 | wgs84_rotating | wgs84_j2 | isa1976 | — | false | 0.1 | — | sea level, v_n = [1000, 0, −1000] ft/s, ψ = 0 |

**Case 8's `wind/table` node placement, corrected.** My first draft put nodes at `h = 0` and
`h = 30,000` ft and claimed the case "never reaches either endpoint" — false as stated: case 8
*starts* at 30,000 ft (9144 m), exactly on the upper node, so only the plugin's clamp-at-endpoint
policy (not "strictly interior") would have covered t = 0. And the fall distance I cited (~4389 m)
was case 1's dragless figure, not case 8's own — case 8 falls from 9144 m to 4965.5 m (§2's own
table), a 4178.5 m descent, not 4389 m. Since the profile is exactly linear
(`V = 0.003h − 20` ft/s) and piecewise-linear interpolation of a linear function is exact between
any two nodes regardless of their spacing, the fix costs nothing physically: place the two nodes at
`h = 0` and `h = 40,000` ft (0 m and 12,192 m — 2/3 of the report's own case 11/12 F-16 cruise
altitude, so 40,000 ft is a defensible upper bound rather than an arbitrary round number) computed
from the same formula (`V(0) = −20` ft/s, `V(40000) = 100` ft/s). Case 8's actually-flown band,
9144 m down to 4965.5 m, is then strictly interior to `[0, 12192]` m, and the clamp policy is never
exercised by any of the ten cases.

**`inverse_square`'s own defaults (`g0 = 9.80665`, `radius = 6371000.0`, matching `earth/round`'s
default) do NOT reproduce cases 4/5** — those use Table 73's sphere radius (6371007.1809 m) and
`μ = 3.986004418e14 m³/s²` (Table 73, atmospheric-check-case row), giving `g0 = μ/R² =
9.820228349842926 m/s²`, not standard gravity. Verified this session against the published
`localGravity_ft_s2` at 30,000 ft: computed `32.126312070521` ft/s² vs. published
`32.126312070546` — agrees to 2.5e-11 ft/s². Both configs must set `g0` and `radius` explicitly;
leaving either at its plugin default silently drops onto the wrong sphere.

Two traps already resolved, both in `ADR-0003`/`docs/lessons.md` territory:

- **`ω_b` is inertial-referenced.** Tables 25–34 print body rates *relative to the rotating Earth*
  (`−0.004178073`, `9.995821927` °/s); the CSV column `bodyAngularRateWrtEi_deg_s_*` and the prose
  both give the inertial values (0, and 10/20/30). Vol II's own "Lessons Learned" (doc p.604)
  records the participants splitting into two camps over exactly this. The configs use the CSV.
- **Earth rate.** Vol II Table 73 gives ω = 7.292115e-5 rad/s, which is `geodesy::kOmegaIe`
  already. The workbook's 7.292113023868e-5 (a back-conversion of 0.004178073 °/s) would put roll
  5.93e-10 rad off at t = 30 against a 1.32e-10 rad band. Settled numerically: for sims 04/05/06,
  `φ(30) = −(ω·30 + λ(30))` holds to **1e-15 rad with Table 73's value** and is 5.9e-10 off with
  the workbook's. The excluded sim_02 is the one that used the workbook's.
- **`localGravity_ft_s2` is not, and must not become, a compared channel.** Verified this session
  at case 1's initial point (equatorial, 9144 m): the published value matches **pure Newtonian
  gravitation with J2** (residual −4.5e-7 ft/s², i.e. J2/J4-order truncation noise) — computing it
  *with* the centrifugal correction (`Ω×(Ω×r)`) instead is 0.111 ft/s² (0.35%) off, six orders of
  magnitude worse. But `gravity/wgs84_j2::g_n()` (`wgs84_j2.cpp:43-53`) deliberately computes
  **apparent** gravity (gravitation minus centrifugal, Groves eq. 2.142 — the case 1 golden's own
  header already cites this), and it must: `Eom`'s translational derivative
  (`core/src/kernel/eom.cpp:35-42`) adds only the Coriolis-like `−(ω_b + ω_ie_b)×v_b` term, no
  separate centrifugal term, so `g_n` is the only place centrifugal can be. ASTRA is self-consistent
  and correct here (case 1's already-accepted independent reference confirms it, computed the same
  way) — the report's own diagnostic column is simply a different physical quantity (gravitation γ,
  not apparent gravity g). The generator's channel list omits `localGravity_ft_s2` for this reason,
  stated so a future session doesn't add it back and chase a phantom 0.35% "bug."

### Why split

`just check` must pass on every preset for the whole thing, and PR 3a is the half that can be
proven with no human-owned data, no guard lift, and no serialization on Joe's availability
mid-session — it is also the half whose physics diff is short enough to read (the drag and damping
forms above, ~40 lines of them). PR 3b is then almost entirely generated data plus a data-driven
test, with one guard-lift window at a known point. Together they are ~1750 hand-written lines; PR 1
split at ~1200. If Joe would rather have one PR, the plan works unchanged — it just means one long
session with the guard armed through the second half and a diff Joe reads in two sittings anyway.

**`just check`'s wall-time budget is why the gate runs locally at all** (`ctest` dominates it;
physics tests are the majority of the runtime, one preset at a time, four presets) — PR 3b replaces
the single existing 30,000-frame `nesc_case1_test.cpp` with ten of them. See the Verification
section below for measuring this before committing to the design, rather than estimating it here.

## Verification

Both PRs, in order, nothing claimed before the line is read. `$SCRATCHPAD` is not an actual
environment variable in a Claude Code session — using it unquoted-and-unset would redirect to
`/check.log` at the filesystem root and fail on permissions, so the tail that's supposed to verify
success would read nothing:

```
just check > "${SCRATCHPAD:-/tmp}/check.log" 2>&1   # via Bash run_in_background
```

Blocking on the right pid needs `docs/lessons.md`'s two 2026-09-16 entries, not a bare
`pgrep -x just`: more than one legitimate `just check` can be running on the machine at once
(other sessions, other worktrees), and `ps aux | grep "just check"` also matches the harness's own
`bash -c 'eval ... just check ...'` wrapper line. Disambiguate by walking up to the wrapper shell
and matching this session's own `CODEX_COMPANION_SESSION_ID` (or `CODEX_COMPANION_TRANSCRIPT_PATH`)
before blocking on any pid:

```
pid=$(ps -eo pid,ppid,cmd | awk '$3=="just" && $4=="check" {print $1}')
# for each candidate pid, walk to its wrapper shell (ppid) and confirm
# CODEX_COMPANION_SESSION_ID in /proc/<ppid>/environ matches this session's own before trusting it
tail --pid=<confirmed pid> -f /dev/null
tail -5 "${SCRATCHPAD:-/tmp}/check.log"             # must show EXIT=0 / check: OK
node "$(find ~/.claude/plugins -name codex-companion.mjs | head -1)" review --base origin/main --wait
node "$(find ~/.claude/plugins -name codex-companion.mjs | head -1)" adversarial-review --base origin/main --wait \
  "ADR-0001 frames/quaternion conventions, ADR-0002 SI-inside/convert-at-boundaries, ADR-0012 per-frame domain validity, ADR-0013 the NESC ensemble band, ADR-0014 the resolution floor (PR 3b only)"
```

Commit before the first review call (new files are invisible to it otherwise). PR 3b's acceptance
is PLAN.md's own: all ten cases within tolerance under every preset. Joe reads the physics diff.

**Wall-time is not something to leave as "worth watching."** The gate runs locally specifically
because of its wall-time budget, and I cannot measure it from plan mode (running `ctest` writes
build artifacts, which plan mode's read-only restriction excludes). **First concrete step of the
implementation session, before writing any PR 3b code:** time the existing
`nesc_case1_test.cpp` once (`ctest --preset asan -R nesc.*Case1` or equivalent, one preset is
enough for an estimate) and multiply by ten to get PR 3b's expected addition per preset, before
committing to the ten-case design as scoped. If that number is uncomfortable, the fix is narrowing
frame count or preset scope, decided then, not guessed now.
