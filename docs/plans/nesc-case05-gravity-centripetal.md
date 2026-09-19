# Brief (spec + plan): `gravity/inverse_square` is missing the centripetal term on a rotating Earth

Status: **approved (Joe, 2026-09-19)**, including D2's resolution below. Implementation proceeding.
Owner: Joe (requirement owner); Claude plans/builds/validates. Codex reviews.
Risk: 3 — `plugins/physical` (a gravity model feeding every EOM step through `Eom::operator()`), plus one
open question (D2 below) that would touch `core/kernel/simulation.cpp` if Joe chooses enforcement.
Consumed by the human-owned NESC oracle comparison in `tests/physics/nesc/**` (no golden file itself is
touched by this fix; two non-golden case configs gain one explicit key each).
Intent/approval: Joe's task instruction (this session), citing `PLAN.md`'s unchecked "Investigate
`case05`'s `altitude`/`v_down` anomaly" item and the "Cowork lead, unverified" hypothesis
(`omega^2*R*t^2/2`) it supplied as a starting point, to be verified rather than trusted. Three further
Cowork/Joe read-throughs (unverified except where noted, code-read-only) received mid-session are folded
in below, most recently a revision that settles the "where does the rotation rate come from" question
differently from this brief's own first draft — see "Design decisions" below.
Architecture/ADRs: `core/include/astra/interfaces/gravity_model.hpp`'s own plumb-bob contract ("g_n is
plumb-bob gravity (gravitation with centripetal folded in) ... so the EOM carries no separate
centripetal term"), `docs/architecture.md` §7. ADR-0003 (position representation, unaffected). **No new
ADR** — this implements a contract already accepted and already correctly met by `gravity/wgs84_j2`; it
is not a new design decision, only D2 below (still open) would be one.
System context: `Eom::operator()` (`core/src/kernel/eom.cpp:40-42`) computes
`v_b_dot = F_b/m + g_b - (omega_b + omega_ie_b) x v_b`, where `g_b = rotate_n_to_b(q_nb, gravity_.g_n(vs))`
— **verified term-for-term against Stevens, Lewis & Johnson eq. 1.7-16b**, and this session's own direct
read of the source PDF confirms it exactly (`docs/refs/`, main checkout, p. 41: `b v_cm/e_dot = 1/m F + g
- (omega_b/i + omega_e/i) x v_cm/e`, eq. 1.7-16b): `eom.cpp` matches it term for term, Coriolis present
explicitly, centripetal folded into `g` only, confirming the plumb-bob contract is the textbook's own
convention, not a local invention, and that the EOM side of this diagnosis is settled — **the gap is in
the gravity plugin alone**, not in `eom.cpp`. `gravity/wgs84_j2` meets this contract by adding
`omega^2 * r_ecef_{x,y}` to its ECEF gravitation before rotating to NED
(`plugins/physical/gravity/wgs84_j2/wgs84_j2.cpp:50-53`, citing Groves eq. 2.133), reading
`astra::geodesy::kOmegaIe` unconditionally. `gravity/inverse_square` does not add anything at all
(`plugins/physical/gravity/inverse_square/inverse_square.cpp:75-80`): `g_n = [0, 0, g0*(R/(R+h))^2]`,
purely radial, never reading latitude, and reads no rotation-rate constant at all.

## Outcome and boundaries

**Outcome**: `gravity/inverse_square`, when the scenario calls for Earth's rotation to matter, supplies
plumb-bob gravity including the centripetal term, matching the contract `gravity_model.hpp` already
states and `gravity/wgs84_j2` already implements. This removes a genuine missing-force bias that a
rotating round-Earth pairing (`earth/round` with `rotating: true` + `gravity/inverse_square`) currently
has no way to supply, which is the leading candidate for case05's disproportionate altitude/v_down miss.

**Non-goals** (unchanged from the task instruction): no `Eom`/`Integrator`/`WrenchSource` change (now
independently confirmed unnecessary, not just assumed — see "System context" above); no golden
regeneration; cases 4/5 stay `GTEST_SKIP()`ed; no change to `wgs84_j2`/`wgs84_rotating`/`earth/round`'s
existing kinematics.

**Invariants preserved**: `gravity/inverse_square`'s default behavior (`rotating` unset/false) is
**bit-identical** to today's — every existing caller (`tests/configs/physics_round_earth_drop.yaml` /
`tests/physics/round_earth_drop_test.cpp`, M2 PR 1's own non-rotating closed-form accept test;
`tests/unit/kernel/simulation_physical_config_test.cpp`; `tests/unit/kernel/simulation_halt_test.cpp`;
`tests/unit/gravity/inverse_square_test.cpp`) configures no rotation and must see no numeric change.
M2 PR 1's non-rotating closed-form drop test (`round_earth_drop_test.cpp`) stays completely untouched —
same config, same expected closed form, same pass/fail — because the correction term evaluates to exactly
zero whenever `rotating` is at its default `false` (see "Design decisions" below for why this holds with
no branch in `g_n()`'s hot path). Cases 1-3/6-10 use `gravity/wgs84_j2`, not touched by this fix, and are
unaffected by construction.

**Assumptions**: whoever pairs `earth/round rotating: true` with `gravity/inverse_square` in a config must
also set `gravity/inverse_square`'s own `rotating: true` (D1 below) — this plugin does not, and under D1
will not, read the Earth model's own config or reach across to it directly; **today's actual case05 bug
is exactly one instance of these two independently-configured booleans disagreeing** (`earth.rotating:
true`, `gravity` with no rotation input at all, i.e. effectively always "false"). Whether that class of
disagreement should be enforced automatically going forward is D2 below, not resolved by this brief on
its own. This is the same class of already-accepted, already-documented responsibility as
`gravity/inverse_square`'s existing `radius`/`g0` needing to be kept consistent with `earth/round`'s own
`radius` (`docs/lessons.md`'s 2026-09-15 "two radii" entry: "two plugins that each read their own
radius... can independently pass their own init-time validation while still dividing by zero together" —
accepted there as a config-authoring discipline, not fixed with a cross-check). **Also true, symmetrically,
of the already-shipped `gravity/wgs84_j2`**: it applies its own centripetal term *unconditionally* (no
config key at all), so pairing it with a *non-rotating* Earth model would already silently add a spurious
centripetal term today — an existing, unflagged instance of the same class of gap, not introduced by this
change and not fixed here (no current config does this, so it is latent, not manifest — noted for D2's
discussion, not actioned by this brief).

## Diagnosis (reading, cross-verified against both source textbooks this session — still not a simulator
## run, see below)

**The missing term.** `Eom::operator()` never adds a separate centripetal term — verified three ways: (1)
`eom.cpp:35-42`'s own comment and code show the Coriolis-like `-(omega_b + omega_ie_b) x v_b` term as the
only rotation-related contribution the EOM itself makes; (2) a term-by-term check against Stevens, Lewis &
Johnson eq. 1.7-16b (below) confirms `eom.cpp` matches the textbook's translational equation exactly,
centripetal folded into `g` only; (3) **this session directly read both source PDFs at
`/home/joe/repos/ASTRA/docs/refs/`** (main checkout — gitignored, confirmed absent from this worktree per
`docs/lessons.md`'s 2026-09-17 entry) rather than trusting supplied page/equation numbers, per Cowork's
own instruction to verify them independently:

- Stevens/Lewis/Johnson, *Aircraft Control and Simulation*, 3rd ed.: p. 23, eq. (1.5-6),
  `a_P/e = F/m + g - 2*omega_e/i x v_P/e`; p. 33, eq. (1.6-27), `g = G - omega_e/i x (omega_e/i x p)` (the
  plumb-bob decomposition itself, exactly as originally cited); p. 40, eq. (1.7-13) (the same
  decomposition written out in full, `G` and the double cross product explicit, not yet using the `g`
  shorthand); **p. 41, eq. (1.7-16b)**, `b v_cm/e_dot = 1/m F + g - (omega_b/i + omega_e/i) x v_cm/e` —
  the exact match to `eom.cpp` cited above.
- Groves, *Principles of GNSS, Inertial, and Multisensor Integrated Navigation Systems*, 2nd ed.
  (`docs/refs/textbooks/`): **p. 70, eq. (2.133), second line** (the NED-resolved form, as Cowork
  identified), `g_b^n = gamma_ib^n + omega_ie^2 * M(L_b) * r_eb^n`, where
  `M(L) = [[sin^2(L), 0, cos(L)sin(L)], [0, 1, 0], [cos(L)sin(L), 0, cos^2(L)]]`. Substituting
  `r_eb^n = (0, 0, -r)` (the position vector from Earth's center to the body, resolved in the body's own
  local NED frame — its Down component is `-r` since the vector points outward/up) reduces this,
  independently of any code in this repository, to exactly the closed form below: `M(L)*(0,0,-r) =
  (-r*sin(L)*cos(L), 0, -r*cos(L)^2)`, matching term for term.

All four citations (page and equation number) are now independently confirmed by direct read, not taken
on trust — and two independent textbook derivations (Stevens/Lewis/Johnson's general vector form,
Groves' NED-resolved form) agree with each other and with this session's own derivation to the last
sign, which is strong (though still not simulator-run) evidence the formula below is correct.
`gravity/wgs84_j2.cpp:50-53` already cites Groves eq. 2.133 for the identical decomposition in ECEF form;
this plugin's header comment cites **both** Stevens/Lewis/Johnson eq. 1.6-27 and Groves eq. 2.133
(second line) once fixed, per Cowork's request.

**Closed form for a sphere** (derived this session, now confirmed against Groves eq. 2.133's own NED form
above rather than only self-consistently re-derived — see R2 below for how the unit test itself must
still cross-check it independently of this plugin's code): at geodetic/geocentric latitude `lat`
(identical on a sphere), radius `r = radius_ + h`,

```
g_correction_n = omega^2 * r * [ -sin(lat)*cos(lat),   0,   -cos(lat)^2 ]
                                 (north)              (east)  (down)
```

added to the existing pure-radial `[0, 0, g0*(radius_/(radius_+h))^2]`. At `lat = 0` (case05's own
latitude) this reduces to a down-only correction, `g_down -= omega^2 * (radius_ + h)`, and the north/east
terms are exactly zero — **a unit test cannot rely on case05's own latitude alone**: an east/west or
north/south sign error in this formula would pass all ten NESC cases, since case05 is the only
rotating-inverse_square case and it sits at the equator where the north term vanishes by construction.
The plan below adds a non-equatorial, both-hemisphere unit test specifically to close that blind spot.

**Order-of-magnitude cross-check (arithmetic only, not a simulator run):**

```
omega = 7.292115e-5 rad/s (Table 73; astra::geodesy::kOmegaIe already carries this exact value)
R = 6371007.1809 m (Table 73, case04/05's own configured radius)
missing_down_accel(lat=0) = omega^2 * R = 0.033878 m/s^2
naive dragless estimate at t=30s: 0.5 * a * t^2 = 15.245 m altitude, a*t = 1.016 m/s (v_down)
observed (R8 dry-run baseline, docs/plans/nesc-harness.md): altitude -13.748 m, v_down +0.801 m/s
```

Same order of magnitude and same sign (ASTRA's altitude/v_down miss means it falls *faster than the
reference*, consistent with a `g_down` that is currently too large by the missing centrifugal reduction).
The ~10-15% shortfall between the naive dragless estimate and the observed values is plausibly case05's
real `C_D = 0.1` drag damping the fall rate over 30 s — offered as a plausible explanation, **not
verified**, since the naive estimate above assumes free fall with no drag at all, which case05 never is.

**Confirmed by an actual dry run (2026-09-19, R4/R8, asan preset, skips locally lifted then restored —
`git diff --stat` on `tests/physics/nesc/nesc_cases_test.cpp` shows zero net change after)**:

| Channel | case04 (pre-fix, unchanged) | case05 pre-fix | case05 post-fix |
|---|---|---|---|
| altitude, t=30s | diff -0.014685 m, ratio 1.495 | diff -13.748270 m, ratio 1408.52 | **diff -0.014545 m, ratio 1.490** |
| v_down, t=30s | diff +0.0015207 m/s, ratio 1.312 | diff +0.800743 m/s, ratio 694.39 | **diff +0.0015042 m/s, ratio 1.304** |
| density | ratio 0.114 (pass) | ratio 37.45 (FAIL) | **0 failures logged (pass)** |
| pressure | ratio 0.157 (pass) | ratio 53.57 (FAIL) | **0 failures logged (pass)** |
| temperature | ratio 1.49 (FAIL) | ratio 1405.84 (FAIL) | **ratio 1.488 (FAIL, matches case04)** |

Case05's altitude/v_down/temperature now land at case04's own pre-existing miss level (both cases'
`t=30s` diffs agree to within ~1% — `-0.014685`/`-0.014545` m, `+0.0015207`/`+0.0015042` m/s;
**corrected, Codex adversarial review**: an earlier draft of this section claimed these "agree to 4
significant figures," which Codex's independent recomputation showed is an overclaim — the two values
agree to within ~1% relative difference, not to 4 significant figures of precision; the substantive
claim, landing at the same order of magnitude and the same tier-3 residual level rather than three
orders of magnitude apart, is unaffected by this correction), not merely "closer": this is exactly the
acceptance's "or at case04's miss level where case04 also misses" clause. Density and pressure now pass
cleanly in both cases (previously case05-only failures).
No channel got worse: cross-checked every channel's max ratio against
`docs/plans/nesc-harness.md`'s own pre-fix baseline table — `longitude` (833.8 vs. 1740.1), `v_east`
(761.5 vs. 1287.0), `yaw`/`pitch`/`roll` (3.44/8.42/6.84 vs. 4.45/10.17/9.80), and even `force_x`/
`force_y`/`force_z` (0 failures logged, vs. case05's own prior 221.6/192.0/273.9 FAILs) all improved or
stayed the same in case05; case04's own numbers are unchanged (its gravity config did not add a
centripetal term, `rotating: false`). `rate_p/q/r`, `moment_l/m/n` unaffected in both cases, as expected.
The hand arithmetic and closed-form derivation above are now corroborated by this run, not standing on
their own.

## Design decisions

### D1 — where does the rotation rate come from: **resolved this session, revising this brief's own first
draft**

This brief's first draft proposed a numeric `omega` config key on `gravity/inverse_square` (reasoning: the
plugin's other constants, `g0`/`radius`, are already config, not hardcoded). **Revised**: `earth/round`
itself takes no numeric rotation-rate config at all — `rotating: <bool>` (default `false`) is the only
knob, and when true it reads `astra::geodesy::kOmegaIe` (7.292115e-5 rad/s, Table 73's own value)
unconditionally; `gravity/wgs84_j2` does the same. Matching that exact shape on `gravity/inverse_square` —
a `rotating: <bool>` config key (default `false`), reading the same `astra::geodesy::kOmegaIe` constant
when true — keeps **one single source of truth for the number** (no config author ever types
`7.292115e-5` a second time), needs no ABI/interface change, and keeps case04's config and M2 PR 1's
non-rotating closed-form test bit-identical, exactly as the rejected numeric-key option also would have.
This does tie `gravity/inverse_square` to a WGS-84-defining constant it does not otherwise depend on
(this brief's own first-draft objection to Option B) — accepted as the smaller cost against a duplicated
literal, since `earth/round` already accepts exactly this tradeoff for the identical constant.

**Implementation shape**: `init()` reads `const bool rotating = config.boolean("rotating", false);` and
sets a single stored member `omega_ = rotating ? astra::geodesy::kOmegaIe : 0.0;`. `g_n()` then
**always** evaluates `omega_^2 * (radius_ + h) * [-sin(lat)*cos(lat), 0, -cos(lat)^2]` unconditionally, no
`if` needed in the per-frame path — at the default `omega_ == 0.0` every added term is exactly `0.0`
(IEEE-754: `0.0 * finite = 0.0`), reproducing today's return value bit-for-bit with no branch and no
special-cased default path to keep in sync with the general one.

**Residual, stated plainly**: this still leaves two independently-configured booleans (`earth.rotating`,
`gravity.inverse_square.rotating`) that a config author must set consistently by hand — **exactly today's
actual bug, restated as a config-authoring requirement rather than eliminated**. D2 below is whether that
residual is acceptable as documented-only or should be enforced.

### D2 — should a mismatched `earth.rotating` / `gravity.rotating` be rejected at init, or disclosed only?
**Open — Joe's call, not resolved by this brief.**

**(a) Disclosed only (no new code).** Cost: zero. Risk: leaves exactly the bug this task fixes able to
recur the moment a future config pairs `earth/round rotating: true` with `gravity/inverse_square` and
forgets the matching key — the same already-accepted posture as the existing `radius`/`g0` duplication
(`docs/lessons.md` 2026-09-15). Also leaves `gravity/wgs84_j2`'s own *unconditional* rotation symmetric
gap (pairing it with a non-rotating Earth model) equally undisclosed-by-code, though it is already
disclosed in this brief's "Assumptions" section above.

**(b) Rejected at init.** Needs a real, if modest, new path: today `GravityModel::init()` sees only its
own `Config` node (`gravity_model.hpp`), with no visibility into the Earth model's resolved config or
state — the same gap `assumed_position_representation()`/`EarthModel::position_representation()` already
closes for position representation, but nothing does yet for rotation. The smallest concrete shape,
mirroring that existing pattern exactly: `EarthModel` gains a new pure-virtual (or defaulted, like
`assumed_position_representation()`) accessor exposing whether it is configured to rotate;
`GravityModel` gains an analogous "what rotation state do I assume" accessor (default: "does not care",
so `gravity/constant` needs no change); `Simulation::from_config` compares the two after both models are
constructed and throws `ConfigError` naming both `physical.earth`/`physical.gravity` paths on a mismatch —
the same shape as the already-existing position-representation check in `from_config`. This is `core/`
work, not `plugins/physical/`, and — while small and precedented, not a novel architecture decision —
sits outside this task's own stated Tier-3/`plugins/physical` framing and its "no `Eom`/`Integrator`/
`WrenchSource` changes" non-goal's spirit (it does not touch those three, but does touch
`core/kernel/simulation.cpp`). **Not implemented by this plan without Joe's explicit go-ahead**; if
authorized, it is additive to the mapping/sequence table below, not a replacement for D1's fix.

**Resolved (Joe, 2026-09-19): (a) for this task.** This brief adds no `core/` cross-check. A new unchecked
`PLAN.md` item is added for the (b) follow-up, scoped explicitly to **both** directions of the mismatch —
not only `earth/round rotating: true` paired with a non-rotating `gravity/inverse_square` (today's actual
bug), but also the symmetric, currently-latent case: `gravity/wgs84_j2`'s own *unconditional* rotation
paired with a non-rotating Earth model (no current config does this, but nothing would catch it either).
That follow-up is not started by this task.

## Contract and failure behavior

- New `rotating` config key (bool, default `false`) on `gravity/inverse_square`, read via
  `config.boolean("rotating", false)`, the identical call `earth/round` already makes.
- `g_n(vs)`: unchanged pure-radial term; unconditionally adds
  `omega_^2 * (radius_ + h) * [-sin(lat)*cos(lat), 0, -cos(lat)^2]` where `lat = vs.p_n()(0)`, `h =
  vs.p_n()(2)` (already read), `omega_` set once at `init()` per D1's implementation shape.
- `in_domain`/`validate_p_n`: **unchanged**. The added terms have no singularity anywhere finite `lat`,
  `radius_ + h` already reach (`sin`/`cos` are bounded, no new division); the existing `radius_ + h > 0`
  domain check already covers everything the new term touches.
- `assumed_position_representation()`: unchanged (`kGeodetic` — the plugin already reads `vs.p_n()(2)`;
  now it also reads `vs.p_n()(0)`, still under the same geodetic assumption `earth/round`/`wgs84_rotating`
  both already satisfy).
- Header comment (`inverse_square.cpp:1-6`) rewritten: drop "purely radial... latitude does not enter"
  (false once `rotating: true`), cite **both** Stevens/Lewis/Johnson eq. 1.6-27 (p. 33, the general
  vector-form plumb-bob decomposition) **and** Groves eq. 2.133 second line (p. 70, the NED-resolved form
  this plugin's closed form reduces to when `r_eb^n = (0,0,-r)`) — both citations independently verified
  by direct PDF read this session, not supplied page numbers taken on trust — the same way `wgs84_j2.cpp`
  already cites Groves alone, and state the default-false/opt-in design plainly, including the D1 residual
  (two booleans that must agree) as an explicit code comment, not only a plan note.

## Acceptance

| ID | Observable requirement | Executable check |
|---|---|---|
| R1 | Default (`rotating` unset/false) `g_n` output is bit-identical to pre-fix for every existing caller | `ctest --preset asan -R 'RoundEarthDrop\|SimulationPhysicalConfig\|SimulationHalt\|InverseSquareGravity'` — all pass unmodified; no expected-value literal in any of these tests changes |
| R2 | The new centripetal term is checked against a real, independent oracle, not a test-local formula compared to itself. **The assertion is on the loaded plugin's own `g_n(vs)` output** (not on two test-local expressions compared to each other — a prior draft of this test worded it as "(a) and (b) agree," which would never exercise `inverse_square.cpp` at all): subtract the already-known pure-radial term `[0,0,g0*(radius_/(radius_+h))^2]` from `gravity.g_n(vs)`'s actual return value, and compare the residual against an oracle built independently of this plugin's own formula — construct the geocentric position on a sphere of radius `radius_+h` at `(lat, lon)`, apply `omega^2 * [x, y, 0]`, rotate to NED with the already-existing `astra::geodesy::ecef_to_ned_dcm(lat, lon)` (reusable: depends only on `lat`/`lon`, not the WGS-84 ellipsoid). **This oracle still shares `astra::geodesy::kOmegaIe` and the `omega^2 * r` factor with the plugin under test (same author, same session)** — not sufficient on its own. Add two further, textually-independent checks: (i) one fully hand-computed literal at `lat = 0, h = 0`, Table 73's radius (`R = 6371007.1809`), asserting `g_n(vs)(2) == g0 - omega^2 * R` to a tight absolute tolerance, using literal numbers written directly into the test, not any shared helper/formula; (ii) one check that the correction magnitude scales with `(radius_ + h)` — evaluate at two different altitudes at the same nonzero latitude and assert the ratio of corrections matches the ratio of `(radius_+h)` values, catching a `radius_`-only or `h`-only bug the single-altitude checks above could not. Both hemispheres and a nonzero longitude are exercised (case05 alone, at the equator, would pass even a sign error in the north component). **Tolerance**: an absolute tolerance scaled to the correction's own characteristic magnitude (`omega^2 * (radius_+h)`), not a relative tolerance against each component's own value — a naive relative tolerance breaks exactly at a near-equatorial latitude like `-1e-3 deg`, where the north component itself is tiny (`~6e-7 m/s^2`) even though the overall correction magnitude is not | New tests in `tests/unit/gravity/inverse_square_test.cpp`: `RotatingCentripetalMatchesAnIndependentEcefConstruction` (the plugin's actual `g_n()` output, residual vs. oracle, at `lat in {-60, -30, -1e-3, 30, 60} deg`, `lon != 0`, absolute tolerance scaled to `omega^2*(radius_+h)`); `MatchesAHandComputedEquatorialValue` (literal `g0 - omega^2*R` at `lat=h=0`); `CorrectionScalesWithRadiusPlusAltitude` (same latitude, two altitudes, ratio check); `IsIndependentOfLongitudeWhenRotating` mirroring `wgs84_j2_test.cpp`'s own pattern |
| R3 | `rotating: false` (explicit or default) reproduces the exact pre-fix `g_n` at a nonzero, nonequatorial latitude too (not only the equator, where the correction is coincidentally down-only), compared **bit-for-bit**, not with `EXPECT_DOUBLE_EQ`. **Why bit-for-bit matters here**: at `omega_ == 0.0`, the north term evaluates as `0.0 * (-sin(lat)*cos(lat))`, which is `-0.0` in the northern hemisphere (positive `sin`, positive `cos`, so the un-negated product is positive, negated is negative, times `+0.0` is `-0.0` per IEEE-754's sign-of-product rule) *before* it is added to the existing north component; `0.0 + (-0.0) = +0.0` once added, so the correct implementation (term ADDED to the existing vector, not returned standalone) does produce `+0.0` and should compare bit-equal to a directly-constructed `+0.0` — but `EXPECT_DOUBLE_EQ` cannot tell `+0.0` from `-0.0` apart at all (they compare arithmetically equal), so it would silently pass even a bug that returned `-0.0` instead (e.g. an implementation that constructs the correction vector standalone and returns it directly rather than adding it in place). Use `std::bit_cast<std::uint64_t>` (the pattern already used by the determinism tests, e.g. `tests/unit/wind/wind_determinism_test.cpp`) so the two sign-of-zero cases are actually distinguished | New test, e.g. `NonRotatingLeavesGCorrectionExactlyZeroAwayFromTheEquator`, asserting `std::bit_cast<std::uint64_t>` equality component-wise against the pre-fix literal `g_n` value |
| R4 | Skip-lifted dry run (temporary, local-only `GTEST_SKIP()` removal, one preset — mirrors `docs/plans/nesc-harness.md`'s own R8 dry-run precedent, never committed): case05's `altitude`, `v_down`, `density`, `pressure`, `temperature` land inside their ADR-0015 bands, **or** at/below case04's own miss level on whichever of those channels case04 also misses (per the existing dry-run baseline table) | `ctest --preset asan -R 'physics.Nesc.Case0[45]'` with the skip locally removed; per-channel `max\|diff\|/tol_abs` captured the same way as the existing baseline table; skips restored before any commit |
| R5 | Cases 1-3, 6-10 unaffected | `ctest --preset {asan,debug,release,static-plugins} -R 'physics.Nesc'` — 8 passed / 2 skipped / 0 failed, every preset |
| R6 | Full gate clean | `./scripts/check-full.sh` (`just check`), all four presets, `check: OK` |
| R7 | Scope stays inside `plugins/physical/gravity/inverse_square/`, its unit test, and the two case04/case05 YAML configs (plus `PLAN.md`/`STATE.md` bookkeeping) — no `tests/golden/**`, `docs/decisions/**`, `core/`, `Eom`, `Integrator`, or `WrenchSource` file touched, unless Joe authorizes D2(b), which is additive and separately scoped | `git diff --stat` against the branch base |
| R8 | Report what still fails in cases 4/5 after the fix (a fresh per-channel table, same shape as `docs/plans/nesc-harness.md`'s existing R8 baseline) — this becomes PR 3c's updated starting point | Same dry-run capture as R4, full channel table, not just the five named channels |

No `tests/golden/**` or `docs/decisions/**` file is touched — `touch-goldens` is **not** needed for this
task (D2(b), if authorized, still touches no golden/ADR file either).

## Repository findings

Observed, this session (file/symbol-cited), beyond what "Diagnosis" above already covers:

- `core/src/kernel/eom.cpp:40-42` — confirms `v_b_dot` has no separate centripetal term; the plumb-bob
  contract is the *only* place it can come from. Cross-verified term-for-term against Stevens/Lewis/
  Johnson eq. 1.7-16b this session.
- `plugins/physical/gravity/wgs84_j2/wgs84_j2.cpp:50-53` — the existing, already-reviewed precedent for
  folding centripetal into `g_n`, in ECEF form, reading `astra::geodesy::kOmegaIe` unconditionally; not
  directly reusable for a plain sphere without generalizing `geodesy.hpp`'s ellipsoid-specific
  `geodetic_to_ecef`/`kSemiMajorAxis`/`kEccentricitySquared` — this plan writes a local, sphere-specific
  closed form in `inverse_square.cpp` instead, matching that file's own existing local-helper style
  (`domain_fault`). `geodesy::ecef_to_ned_dcm` itself *is* reusable as-is for the R2 test's independent
  construction, since it depends only on `(lat, lon)`, not on ellipsoid shape.
- `plugins/physical/earth/round/round.cpp:84` — `config.boolean("rotating", false)`, the exact call/
  default `gravity/inverse_square` will mirror under D1.
- `tests/physics/nesc/case04_dropped_sphere_round_non_rotation.yaml` /
  `case05_dropped_sphere_round_rotation.yaml` — today identical in their `physical.gravity` block; only
  `physical.earth.rotating` differs. This is the direct evidence that `gravity/inverse_square` does not
  respond to the Earth model's own rotation state at all — i.e., today's actual bug is precisely D1's
  named residual (the two booleans disagreeing) already realized in these two files.
- `tests/configs/physics_round_earth_drop.yaml` + `tests/physics/round_earth_drop_test.cpp` — M2 PR 1's
  own non-rotating closed-form accept test; uses `gravity/inverse_square`'s bare defaults (no `radius`/
  `g0`/now `rotating` override) — confirmed this stays byte-identical under D1.
- `tests/unit/kernel/simulation_physical_config_test.cpp`'s
  `RejectsAnInverseSquareRadiusInconsistentWithTheInitialAltitude` and
  `tests/unit/kernel/simulation_halt_test.cpp`'s inverse-square usage — neither sets rotation-related
  config; unaffected by construction (D1's `rotating` defaults to `false`).
- `docs/lessons.md:27` (2026-09-15) — the accepted precedent for "two independently-configured plugins
  each read their own copy of a physically-shared number and are not cross-checked," cited above as
  today's accepted posture for the residual D1 leaves, and the basis for D2(a)'s cost/risk framing.
- `docs/lessons.md:47` (2026-09-17) — `docs/refs/` is gitignored and absent from this worktree, but is
  present and readable at the main checkout's absolute path (`/home/joe/repos/ASTRA/docs/refs/` and
  `.../docs/refs/textbooks/`); this session read both source PDFs directly at those paths rather than
  trusting supplied page/equation numbers (per Cowork's explicit instruction) — see "Diagnosis" above for
  the four citations confirmed and the one further cross-check (Groves eq. 2.133's NED form reducing to
  this plugin's own closed form) this direct read made possible that a page-number citation alone would
  not have.

Assumption-vs-repository mismatches: none found. The task instruction's "Cowork lead, unverified"
hypothesis is supported by the code read, the term-by-term EOM cross-check, the two-textbook closed-form
cross-check, and the order-of-magnitude arithmetic above, but is explicitly not yet confirmed by a run
(see "Confirm before trusting").

## Mapping and sequence

| Requirement | Change / owned paths | Dependencies | Acceptance command |
|---|---|---|---|
| D1, Contract | `inverse_square.cpp`: add `#include "astra/util/geodesy.hpp"`, `omega_` member set at `init()` per D1's shape, extend `g_n()`, rewrite header comment (cite eq. 1.6-27, disclose the D1 residual) | Joe's approval of D1 (recommended, stated above) | Compiles; R1 |
| R1, R3 | No change needed to any existing test file's expectations — regression coverage only | Needs the plugin change | Existing tests rerun unmodified |
| R2 | New tests in `tests/unit/gravity/inverse_square_test.cpp` | Needs the plugin change | `ctest --preset asan -R InverseSquareGravity` |
| R4, R8 | Case04/05 YAML: add explicit `physical.gravity.rotating` (`false` / `true`); temporary local dry run | Needs the plugin change, R1-R3 green | `ctest --preset asan -R 'physics.Nesc.Case0[45]'` with skips locally lifted, then restored |
| R5, R6 | Full gate | All of the above | `./scripts/check-full.sh`, all 4 presets |
| R7 | Scope check | Continuous | `git diff --stat` |
| D2(b), if authorized | `earth_model.hpp`/`gravity_model.hpp`: new accessor pair; `simulation.cpp`: `from_config` cross-check, mirroring the existing position-representation check | Joe's separate authorization; not assumed | New test(s) in `tests/unit/kernel/simulation_physical_config_test.cpp` |

One Claude builder, single package (one plugin, one unit-test file, two config files) for D1; D2(b), if
authorized, is additive to the same package (small enough not to need its own decomposition).

## Validation and risks

- **Primary risk**: a sign or frame error in the closed-form correction. Mitigated by: two independent
  textbook derivations already agreeing with this session's own derivation to the last sign (Stevens/
  Lewis/Johnson's general vector form and Groves' NED-resolved form, both directly read this session, not
  taken on trust); R2's ECEF-plus-rotation cross-check against the plugin's own `g_n()` output, plus a
  hand-computed literal and a radius-scaling check that share no formula/constant with the implementation
  under test; and the dry-run's own before/after diff — a wrong-signed fix would make case05's miss
  *larger* or flip its sign, immediately visible in R4/R8's table, not silently accepted.
- **Secondary risk**: the fix may not fully close case05's gap to case04's level (drag/spin coupling
  interactions this plan does not model are possible) — R4's acceptance already allows landing at case04's
  own miss level rather than exactly zero, and R8 requires reporting whatever still fails plainly, not
  diagnosing or forcing it further.
- Zero risk to any currently-passing case/test: the new term is exactly zero unless a config explicitly
  sets `rotating: true`, which today only case05's edited config will do.
- **D1's own residual risk** (stated, not eliminated by this task unless Joe authorizes D2(b)): a future
  config pairing `earth/round rotating: true` with `gravity/inverse_square` and omitting the matching
  `rotating: true` reproduces today's exact bug silently. Disclosed in the plugin's own header comment
  per "Contract and failure behavior" above, regardless of D2's outcome.
- **Rollback**: revert the plugin, unit-test, and two-config diff; trivial, no data migration.
- **Compatibility**: additive, opt-in config key; no `GravityModel` interface/vtable change, no
  `kCoreApiVersion` bump (D1 alone). D2(b), if authorized, does change `EarthModel`/`GravityModel`'s
  vtable (a new virtual accessor on each) and would need the same `kCoreApiVersion` bump precedent PR 2c's
  `in_domain` addition already set.
- **New dependency**: none.
- **Human-owned files touched**: none (`touch-goldens` not needed) for D1 or D2(b).

## Plan critique and decisions

No separate Codex plan critique dispatched for D1, for the same reason `docs/process/workflow.md` allows
skipping it: the design is pinned by already-accepted architecture (`gravity_model.hpp`'s own plumb-bob
contract, now cross-verified against Stevens/Lewis/Johnson term-for-term) and by `wgs84_j2`'s and
`earth/round`'s own already-reviewed precedent for exactly this constant/config shape — this brings a
plugin into compliance with a standing contract it currently violates, using a shape two sibling plugins
already use, not a new unpinned design. `/review-cycle`'s ordinary post-implementation independent **and
adversarial** review (tier 3: sign/frame/units correctness, exactly what a trig derivation like this most
needs) still applies before the human-review packet, and Codex should be asked specifically to re-derive
the closed form independently, not just read it, given the case05-blind-spot concern above. **If Joe
authorizes D2(b)**, that specific addition (a new cross-plugin interface accessor pair plus a
`Simulation::from_config` check) should get its own plan-critique consideration at that time, since it is
a small but real interface change to `core/`, not folded into "already pinned" the way D1 is.

## Questions and decision

| # | Question | Answer/reference |
|---|---|---|
| D1 | Where does the rotation rate come from | **Resolved this session**: `rotating: <bool>` (default `false`) on `gravity/inverse_square`, reading `astra::geodesy::kOmegaIe` when true — mirrors `earth/round`'s own shape exactly, single source of truth for the constant. Supersedes this brief's own first-draft numeric-`omega`-key proposal. |
| D2 | Should a mismatched `earth.rotating`/`gravity.rotating` be rejected at init (needs a new `core/` cross-check path) or disclosed only? | **Resolved (Joe, 2026-09-19): (a), disclosed only, for this task.** A new unchecked `PLAN.md` item tracks (b) as a separate follow-up, covering both mismatch directions (`gravity/inverse_square` and `gravity/wgs84_j2`). |

Joe's spec/plan approval: **approved, 2026-09-19.** Proceeding to implementation.

## Done when

- [x] R1: default (`rotating` unset/false) `g_n` bit-identical to pre-fix — `ctest --preset asan -R
      'RoundEarthDrop|SimulationPhysicalConfig|SimulationHalt|InverseSquareGravity'`, 47/47 pass, all
      four presets (asan/debug/release/static-plugins) verified individually, `100% tests passed, 0
      tests failed` each.
- [x] R2: independent oracle cross-checks (ECEF-plus-rotation, hand-computed equatorial literal,
      radius-scaling) all pass — 8/8 `InverseSquareGravity.*` tests pass under asan (caught and fixed
      one real bug in the test helper itself along the way: `std::to_string(double)`'s 6-decimal
      truncation silently corrupted `g0`'s precision, off by exactly the truncated amount — fixed with
      a `max_digits10` formatter before trusting the test).
- [x] R3: bit-cast comparison at a nonzero, nonequatorial latitude passes — confirms the
      add-to-existing-vector implementation shape (not a standalone `{north,0,down}` construction)
      that the +0.0/-0.0 analysis required.
- [x] R4, R8: skip-lifted dry run (asan, skips restored, `git diff` on `nesc_cases_test.cpp` confirmed
      zero net change after) — case05's `altitude`/`v_down`/`temperature` now match case04's own
      pre-existing miss level to within ~1% (not "4 significant figures," this session's own
      overclaim, corrected per Codex adversarial review); `density`/`pressure` now pass cleanly in
      both cases; every other channel improved or unchanged (full table in "Diagnosis" above).
      Reported to `PLAN.md` and `docs/plans/nesc-harness.md` as the new baseline for these five
      channels.
- [x] R5: cases 1-3/6-10 pass under all four presets — `ctest --preset {asan,debug,release,
      static-plugins} -R 'physics.Nesc'`, `100% tests passed, 0 tests failed out of 10` each (8
      passed, 2 skipped).
- [x] R6: full gate clean — `just check`, `check: OK`, exit 0, all four presets `100% tests passed, 0
      tests failed out of 288`, pyright/pytest clean. (First run failed `format-check` on the new test
      file; fixed with `clang-format-18 -i` and reran clean — recorded honestly, not glossed over.)
- [x] R7: scope check — `git diff --stat` touches only
      `plugins/physical/gravity/inverse_square/inverse_square.cpp`,
      `tests/unit/gravity/inverse_square_test.cpp`, the two case04/case05 YAML configs, `PLAN.md`, and
      this doc/`nesc-harness.md`'s bookkeeping. No `tests/golden/**`, `docs/decisions/**`, `core/`,
      `Eom`, `Integrator`, or `WrenchSource` file touched; `touch-goldens` never needed.
- [ ] Independent Codex review (numerical/adversarial, tier 3: sign/frame/units correctness) —
      not yet dispatched; next step.
- [ ] Human-review packet — after the review above completes with no unresolved critical/major
      findings.
