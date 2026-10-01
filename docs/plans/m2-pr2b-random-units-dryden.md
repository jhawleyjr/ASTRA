# M2 PR 2b — `core/util/random.hpp`, `core/util/units.hpp`, `plugins/physical/wind/dryden`
# (+ a separate docs-only commit: ADR-0012 and its PLAN.md wiring)

Recovered from `~/.claude/plans/m2-pr-2b-plan-md-zany-flame.md` on 2026-09-30. Approval: the file records none; the work merged as #26. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31). Spelling normalized to American English.

## Context

`PLAN.md`'s M2 PR 2b line is the specification and it already settles the design; this plan does not
re-decide it. PR 2 (merged, #25) shipped `WindModel` with the three-way `step` / `begin_frame` /
`wind_n` split and the per-vehicle `EnvironmentSample` latch, but the only wind plugin is
`wind/uniform`, a constant model — so nothing in the tree yet exercises seeded stochastic evolution,
and PR 2's bitwise determinism harness passes trivially. M2's own row (`docs/architecture.md` §16)
still owes "Dryden PSD matches MIL-F-8785C". This PR adds the RNG utility AGENTS.md pins
(`std::mt19937_64` + in-house transforms, because `std::normal_distribution` is unspecified across
libstdc++/libc++), the unit-conversion utility ADR-0002 names and that does not exist yet, and the
`dryden` plugin that is the first real consumer of both.

Intended outcome: a MIL-F-8785C Dryden turbulence plugin whose PSD is checked against the spec at a
fixed seed, whose gust sequence and registry snapshot are bitwise reproducible across runs and across
`reset()`, and which allocates nothing and throws nothing per frame.

A second, unrelated, docs-only commit lands the already-accepted ADR-0012 (per-frame domain validity)
and its PLAN.md wiring, so it is on `main` before PR 2c implements it.

---

## Part A — commit 1, docs only (ADR-0012)

Verbatim content supplied by Joe; not re-decided here.

Joe's clarification of the ADR's "the monitor already has the Earth model and the atmosphere model":
that is about **interfaces, not members**. `valid_range()` already exists and `AtmosphereModel` gains
no virtual; `Eom::operator()` does not change (ADR-0004). PR 2c is therefore not a pure addition to
`HealthMonitor` — it routes the Earth and gravity models through to the new check and gives
`Simulation` a non-owning `AtmosphereModel*` it does not hold today (PR 1b's `valid_range()` check
runs inside `from_config` against a local that goes out of scope; `Simulation` holds `earth_`, added
by PR 2 for `WindModel::begin_frame`, but no atmosphere pointer; `Vehicle::integrate` reaches an
`EarthModel` only through `Eom`'s reference member). Recorded in the PR body, not in the ADR.

1. `docs/decisions/0012-per-frame-domain-validity.md` — created with exactly the supplied text.
   **Guarded by `.claude/hooks/guard-oracle.sh`**. Per `docs/lessons.md` 2026-09-16: the marker has to
   be touched *mid-session*, in this worktree's own `.claude/` (its `CLAUDE_PROJECT_DIR`) or in the
   main checkout's — a pre-launch touch is deleted by the SessionStart `disarm-goldens.sh` hook before
   this session ever starts, so "arm before I start" means a `touch .claude/touch-goldens` run now,
   while this session is live, not something done ahead of time. I check the marker is present
   immediately before the Write and stop rather than working around the guard if it is not.
   **One more guarded path in this same PR, discovered during planning**: B2.5/B3 below add
   `tests/golden/wind/mil_f_8785c_dryden.hpp`, a new file under `tests/golden/**`, transcribing the
   MIL-F-8785C constants Joe supplied — the same marker covers it, and I create it in the same armed
   window as this ADR file rather than asking twice.
2. `PLAN.md` — insert the supplied PR 2c line between the PR 2b and PR 3 lines, verbatim.
3. `PLAN.md` — the three existing "disclosed not fixed" domain-validity passages (PR 1's
   polar-approach gap, PR 1b's mid-run atmosphere-range gap, PR 2's re-acceptance of both) each gain
   one appended sentence: the gap is now scheduled as PR 2c under ADR-0012, rather than open-ended.
   Nothing is deleted — the disclosures are the record of what shipped. **A fourth edit, not
   instructed**: PR 2b's own line (this PR) also inherits the same disclosure ("Inherits PR 2's
   disclosed per-frame `EarthModel::altitude()` domain-validity gap") and gets the same pointer, for
   consistency with the other three. Joe's instructions named three passages; I'm adding this fourth
   one for the same reason and will call it out as my own addition, not requested, in the PR body —
   easy to drop if it's not wanted.
4. `docs/decisions/0000-founding-decisions.md` has no enumerated ADR index; it cites ADR numbers per
   table row. Decided with Joe: the "Health monitoring" row's reference cell becomes
   `` `architecture.md` §15, ADR-0012 `` — one table cell, matching the per-row citation style
   ADR-0004/0009/0010/0011 already get. No decision text touched. Also guarded.

Commit 1 lands **before** commit 2 (decided with Joe), so the ADR is the earlier, standalone commit
on the branch and `git diff --base origin/main` for review shows both.

---

## Part B — commit 2, `core/util` + `plugins/physical/wind/dryden`

### B1. `core/include/astra/util/units.hpp` (new, header-only)

Follows `geodesy.hpp`/`frames.hpp`: header-only, `constexpr inline` constants and functions in
`namespace astra::units`, no new CMake entry. ADR-0002 §Consequences: "every boundary conversion is a
named function in `core/util/units` with a unit test; nothing multiplies by `0.3048` inline".

- `kMetersPerFoot = 0.3048` (exact, by definition of the international foot).
- `kMetersPerSecondPerKnot = 1852.0 / 3600.0` (exact: 1 kt = 1 nautical mile / hour).
- **Implementation pinned** (Joe's review — the exactness claims below depend on it):
  `feet_to_meters(ft) = ft * kMetersPerFoot`; `meters_to_feet(m) = m / kMetersPerFoot` (division by
  the same constant, not multiplication by a precomputed reciprocal, which would round
  `1/0.3048` once and lose bit-exact round-tripping); `knots_to_meters_per_second(kt) = kt *
  kMetersPerSecondPerKnot`; `meters_per_second_to_knots(mps) = mps / kMetersPerSecondPerKnot`.

`tests/unit/util/units_test.cpp`: `static_assert` on the defining constants; exact equality on
`feet_to_meters(1.0) == 0.3048` and, because of the division-based implementation above,
`meters_to_feet(0.3048) == 1.0` exactly (`x / x == 1.0` for any finite nonzero double); reference
points (10 000 ft = 3048 m exactly, since `10000 * 0.3048` is exact in double); 1 kt asserted against
`1852.0 / 3600.0` computed at the assertion site (**not** a rounded decimal literal — `1852.0/3600.0
== 0.51444444444444...`, and a literal like `0.514444` is off by 4.4e-7, which a 1e-12 tolerance
would fail on); round-trips within 1 ULP where division/multiplication aren't bit-exact.

### B2. `core/include/astra/util/random.hpp` (new, header-only)

`namespace astra::random`, one class `Rng`, per-frame-safe (`noexcept`, no allocation, no exceptions):

```cpp
explicit Rng(std::uint64_t seed) noexcept;
void reseed(std::uint64_t seed) noexcept;   // reset(): also clears the Box-Muller cache
[[nodiscard]] double uniform01() noexcept;  // [0, 1)
[[nodiscard]] double uniform(double low, double high) noexcept;
[[nodiscard]] double normal() noexcept;     // Box-Muller, second variate cached
[[nodiscard]] std::uint64_t seed() const noexcept;
```

- `uniform01()` is `(engine() >> 11) * 0x1.0p-53` — the portable 53-bit construction, not
  `std::uniform_real_distribution` (unspecified across standard libraries, AGENTS.md §Randomness).
- `normal()` is Box-Muller on `u1 = 1.0 - uniform01()` (so the log argument is in `(0, 1]`, never 0)
  and `u2 = uniform01()`; it returns `r*cos(theta)` and caches `r*sin(theta)`. `reseed` clears the
  cache, so a reset sequence is identical to a fresh `Rng(seed)` — the PR's `reset()` acceptance.
- `mt19937_64` holds a fixed-size array; nothing here allocates.

`tests/unit/util/random_test.cpp`: same seed → bitwise-identical sequences (`std::bit_cast`);
different seed → different; `reseed` after partial consumption equals a fresh `Rng`, including when a
cached normal was outstanding; `uniform01()` stays in `[0, 1)` and never returns 1.0 over 1e6 draws;
`normal()` mean/variance and the fraction inside 1σ within stated statistical bounds over 1e6 draws;
no NaN/Inf.

### B2.5. Provenance — resolving the blocking sourcing question

Joe supplied `docs/refs/MIL-F-8785C.pdf` (95 pp., already in the tree, untracked-by-guard —
`docs/refs/` is not matched by `guard-oracle.sh`'s `docs/decisions/|tests/golden/|tolerance` regex).
Read directly (`pdftotext`/`pdftoppm` + visual inspection of the rendered pages, not OCR text alone —
the scan's OCR layer is heavily garbled) and cross-checked against MathWorks' "Dryden Wind Turbulence
Model (Continuous)" documentation (the link Joe gave as a secondary, cleanly-typeset source), which
states the same constants for the MIL-F-8785C variant explicitly. Every constant B3/B4 uses now has a
page citation:

- **§3.7.1.2 "Turbulence model (Dryden form)", p.47**: the three `Phi_u`/`Phi_v`/`Phi_w` spectra in
  terms of `sigma`, `L`, spatial frequency `Omega` — the forms B3's `G_u`/`G_vw` transfer functions
  already matched (Joe: "VERIFIED, do not re-derive").
- **§3.7.2 "Medium/high-altitude model", §3.7.2.1, p.48**: isotropic above 2000 ft
  (`sigma_u=sigma_v=sigma_w`, `L_u=L_v=L_w`); scale length **`L_u=L_v=L_w = 1750 ft` using the Dryden
  form** (the von Karman form's 2500 ft is not used — this plugin is Dryden-only, per PLAN.md).
  §3.7.2.2 (same page): intensities read off Figure 7 (p.49), a probability-of-exceedance chart — the
  source of B4's disclosed `sigma_high` gap, confirmed genuinely undigitized in the primary source.
- **§3.7.3.1/§3.7.3.4, p.51/53**: `W_20` (wind speed at 20 ft) and, p.53, **`sigma_w = 0.1*u20`**
  (mixed units: `sigma_w` in ft/s, `u20` in knots, exactly the boundary conversion B4's `w20_knots`
  config key performs).
- **Figure 10 "Low-altitude turbulence integral scales", p.55** (verified by direct visual read of
  the rendered page, high-DPI — the printed formula box, not the OCR layer, which mis-scans the
  superscript): **`L_u = L_v = h / (0.177 + 0.000823*h)^1.2` for `10 < h < 1000 ft`** — a fraction,
  confirmed by both the visible horizontal bar in the scan and independently by MathWorks' rendering
  of the same MIL-F-8785C formula. `L_w = h` for the same band is **not spelled out as text on this
  page** (the figure's second, unlabeled curve implies it, but MIL-F-8785C's own page doesn't state
  it in words) — confirmed instead from MathWorks' page, which states explicitly, citing MIL-F-8785C:
  "`Lw = h`". Both sources agree; `dryden_filter.hpp`'s comment cites both.
- **Figure 11 "Horizontal turbulence RMS intensities", p.56** (same direct-read method):
  **`sigma_u/sigma_w = sigma_v/sigma_w = 1 / (0.177 + 0.000823*h)^0.4` below 1000 ft** — confirmed
  exponent `0.4`, cross-matches MathWorks.
- **1000–2000 ft blend**: not stated as a formula in the primary MIL-F-8785C text itself (the
  document treats the two regimes separately); MathWorks' page states explicitly that the standard
  implementation "linearly interpolat[es] between the value from the low-altitude model at 1000 feet
  ... and the value from the high-altitude model at 2000 feet" — confirming B3's interpolation choice
  as standard practice, not an invention, while `dryden_filter.hpp`'s comment is explicit that this
  specific band is bridged by implementation convention cross-confirmed via MathWorks, not read
  verbatim off a MIL-F-8785C page number the way the two endpoints are.

**New golden reference, `tests/golden/wind/mil_f_8785c_dryden.hpp`** (guarded path —
`tests/golden/**` — needs the same armed session as Part A's ADR commit; created only once Joe's
marker is confirmed present, same check as Part A item 1): follows `isa1976_table.hpp`'s pattern —
the transcribed constants (`0.177`, `0.000823`, the `1.2`/`0.4` exponents, `1750.0` ft, `0.1` in
`sigma_w=0.1*u20`) as named `constexpr` values, each with an inline comment citing the exact page and
figure/section above, kept **separate from** `dryden_filter.hpp`'s implementation. `dryden_filter_test.cpp`
(B3's test, below) computes its reference values from *this* header's constants, independently of
whatever `dryden_filter.hpp` itself does with them — so a bug in the implementation's transcription
(a flipped exponent, a wrong constant) is still caught, rather than the test and the implementation
silently sharing one (possibly wrong) source. This is what actually closes item 1: not "computed by
hand from spec formulas I recall," but "computed from a separately-transcribed, page-cited golden
header, cross-checked against the PDF and against MathWorks independently."

### B3. `plugins/physical/wind/dryden/dryden_filter.hpp` (new, header-only, plugin-internal)

The physics, split out of `dryden.cpp` so a unit test can check it deterministically. Included by the
test as `"plugins/physical/wind/dryden/dryden_filter.hpp"` — the repo root is already an include dir
for anything linking `astra::core` (`icd/CMakeLists.txt`), the same route
`tests/unit/icd/icd_test.cpp` uses for `"icd/astra_icd.h"`. No CMake change.

**MIL-F-8785C parameters** (`mil_f_8785c_parameters(altitude, sigma_w_low, sigma_high)`), altitude in
meters, all output SI. **Correction to my own B2.5 wording**: `dryden_filter.hpp` does **not**
`#include` the golden header — `plugins/` is shipped production code and must never depend on
anything under `tests/`, which isn't part of the built library. It carries its own independently
page-cited constants (same citations as B2.5); the golden header is what the *test* cross-checks
against, so the two are typed twice, on purpose, and a mismatch between them is exactly what
`dryden_filter_test.cpp` below is for. The spec's laws are written in feet; the constants are
converted **once**, at namespace scope, through `units::` — never inside the per-frame math (Joe's
ADR-0002 point):

- `h <= 1000 ft` (MIL-F-8785C §3.7.3.4/Figure 10/Figure 11, p.53/55/56 — see B2.5): `L_w = h`;
  `L_u = L_v = h / (0.177 + 0.000823*h_ft)^1.2`; `sigma_w = sigma_w_low`;
  `sigma_u = sigma_v = sigma_w / (0.177 + 0.000823*h_ft)^0.4`. The bracket is rewritten in meters as
  `0.177 + (0.000823/kMetersPerFoot)*h`, and `L_u` is scale-free in the unit it is evaluated in, so
  the law evaluates entirely in SI. Both regimes are continuous at 1000 ft
  (bracket = 1, `L_u = L_v = L_w = 1000 ft`, `sigma_u = sigma_v = sigma_w`).
- `h >= 2000 ft` (§3.7.2.1, p.48): `L_u = L_v = L_w = 1750 ft`; `sigma_u = sigma_v = sigma_w =
  sigma_high`.
- `1000 ft < h < 2000 ft`: linear interpolation in `h` between the two — standard implementation
  practice, cross-confirmed via MathWorks (B2.5), not itself printed as a formula in MIL-F-8785C.
- `h` is clamped below at 10 ft (the lowest altitude the spec's own low-altitude curves are drawn to,
  Figure 10's own stated domain `10 < h < 1000 ft`) so `L_w -> 0` cannot divide by zero; commented as
  such. `h` is the Earth-derived `altitude_m`, i.e. height above the configured Earth model's
  reference surface, **treated as height above ground** — there is no terrain model in v1. Disclosed
  in the header.

**Discretization — decided: exact (matched), not Euler-Maruyama.** Each gust axis is an LTI shaping
filter driven by unit-intensity white noise, stepped by its exact discrete-time equivalent, so the
stationary variance is exactly `sigma^2` at any `dt` and no discretization bias hides inside the PSD
tolerance. Conventions pinned in the header: MIL-F-8785C's `Phi(omega)` is **one-sided** in `omega`
rad/s (`sigma^2 = integral_0^inf Phi domega`; verified: both the `u` and the `v`/`w` forms integrate
to `sigma^2`), and for `y = G(s)*eta` with `E[eta(t)eta(t+tau)] = delta(tau)`,
`Phi_y(omega) = |G(j*omega)|^2 / pi`.

- Longitudinal (`u`), with `a = V / L_u`: `G_u(s) = sigma_u*sqrt(2a)/(s+a)`, an OU process. Exact
  AR(1): `u <- exp(-a*dt)*u + sigma_u*sqrt(-expm1(-2*a*dt))*xi`. `expm1` avoids the cancellation in
  `1 - exp(-x)` at the `a*dt ~ 1e-4` this model actually runs at.
- Lateral/vertical (`v`, `w`), with `a = V / L`: `G(s) = sigma*sqrt(a)*(sqrt(3)*s + a)/(s+a)^2`.
  Canonical two-state form `A = [[0,1],[-a^2,-2a]]`, `B = [0,1]'`,
  `y = sigma*sqrt(a)*(a*x1 + sqrt(3)*x2)`. Exact step: `Ad = exp(-a*dt)*[[1+a*dt, dt],[-a^2*dt,
  1-a*dt]]`, `Qd = integral_0^dt exp(-2*a*tau)*[[tau^2, tau*(1-a*tau)],[.., (1-a*tau)^2]] dtau`,
  closed form in `I0, I1, I2 = integral tau^k exp(-2*a*tau)`, then a 2x2 Cholesky and two normals.
  `I1`/`I2` lose all significance as `a*dt -> 0`, so the helper switches to the leading Taylor series
  below a stated crossover (`2*a*dt < 1e-3`).
- **`a` is floored, not special-cased at zero** (Joe's review: `y = sigma*sqrt(a)*(a*x1 +
  sqrt(3)*x2)` goes to *zero* as `a -> 0`, it does not "freeze" — that comment language was wrong and
  is dropped; also, at true `a == 0` the stationary-start divisions below are `0/0`, and even a
  tiny-but-nonzero `a` such as `1e-200` overflows `x1 = xi/(2*a^1.5)` to `Inf` while the branch
  guarding it never fires). `a = V / L` is floored at `a_min = 1e-6 s^-1` (`V` on the order of a
  millimeter per second at a typical `L` — far below any airspeed this sim will ever see, stated as
  such in the comment). At `a_min`, `1/(2*a_min^1.5) = 5e8`, finite and nowhere near overflow, and
  `Var(y) = sigma^2` stays exact regardless of `a` (verified, see below) — so `V` at or near zero
  produces small-but-finite, statistically correct gusts, never `Inf`/`NaN` and never a discontinuous
  special case.
- **Stationary start, no warm-up transient.** The continuous stationary covariance of the canonical
  state is `diag(1/(4a^3), 1/(4a))` with **zero cross-term**, so `x1` and `x2` are statistically
  independent and must be drawn from **two independent normals**, not one reused value (Joe's review
  — the earlier draft's `x1 = xi/(2*a^1.5), x2 = xi/(2*sqrt(a))` used the same `xi` for both, which
  is wrong even though the marginal variances would look right in a variance-only check). The first
  frame after `init`/`reset` instead initializes `u = sigma_u*xi_u`, `x1 = xi_1/(2*a^1.5)`, `x2 =
  xi_2/(2*sqrt(a))` from three independent draws per two-state filter's own pair plus the scalar
  filter's one — five draws total across `u`/`v`/`w`, the same count a normal frame consumes (`u`: 1;
  each of `v`,`w`: 2), so the per-frame draw count stays constant whether or not this frame is the
  stationary-start frame. Without this, every run would open with several `L/V` of artificially
  suppressed turbulence.
- **Frozen-coefficient approximation, stated explicitly** (Joe's review): `a`, `sigma`, `L`, and the
  derived `Ad`/`Qd` are recomputed every frame from that frame's `V`/`h`, but the filter state
  (`u`, `x1`, `x2`) carries forward values shaped by the *previous* frame's coefficients — this is
  the standard quasi-stationary MIL-F-8785C implementation (coefficients assumed slowly varying
  relative to the filter time constants), not an exact time-varying LTV solution, and the header says
  so rather than leaving it implied.

`tests/unit/wind/dryden_filter_test.cpp` (fast, `unit`):

- The MIL parameter laws at 10, 100, 500, 1000, 1500, 2000, 5000 ft against values computed **in the
  test, independently, from `tests/golden/wind/mil_f_8785c_dryden.hpp`'s transcribed constants**
  (B2.5) — not by re-deriving from the implementation's own formula, and not from recalled spec text.
  Continuity at the 1000 ft and 2000 ft regime joins; the 10 ft clamp.
- **Lyapunov residual**: for `a*dt` spanning 1e-8 … 10, assert
  `P - Ad*P*Ad' - Qd ≈ 0` with `P = diag(1/(4a^3), 1/(4a))` to ~1e-12 relative. This checks `Ad`,
  `Qd`, and the series branch together, deterministically, without any statistics — it is the real
  proof that the discretization is exact.
- Series/direct agreement across the crossover; `Qd` positive semi-definite; `a == 0` gives `Qd == 0`.

### B4. `plugins/physical/wind/dryden/dryden.cpp` + `CMakeLists.txt` (new)

`astra_add_plugin(SOURCES dryden.cpp)`, descriptor `{"physical", "wind_dryden", ...}`, `name()` =
`"wind"` (same as `uniform`; only one wind model loads).

**Config** (`physical.wind`), all validated at init with `ConfigError` naming the dotted path, via the
same plugin-local `keyed_path` helper `uniform.cpp`/`round.cpp`/`constant.cpp` each carry:

| key | required | meaning |
|---|---|---|
| `plugin` | yes | `plugins/physical/wind/dryden` |
| `seed` | **yes** | `std::mt19937_64` seed. §15's determinism guard: "seeds present for every stochastic plugin". **No default** — a missing key is a `ConfigError` naming `physical.wind.seed`, never a silent default seed. Rejected: negative, or `> 2^53` (the registry stores doubles, so a larger seed could not be logged exactly; the error says so). |
| `w20_knots` | yes | MIL-F-8785C's `W_20`, the wind speed at 20 ft, **in knots** — the spec's own parameter in the spec's own unit, converted once at config read (ADR-0002: conversions at boundaries). `sigma_w_low = feet_to_meters(0.1 * w20_knots)`, the spec's mixed-unit `sigma_w[ft/s] = 0.1*W_20[kt]`, commented as such. Rejected: non-finite or negative. |
| `sigma_high` | yes | `sigma_u = sigma_v = sigma_w` at and above 2000 ft, m/s (SI). MIL-F-8785C reads this off Figure 10's probability-of-exceedance chart; **that chart is not digitized here** — it would be a human-owned golden table (`tests/golden/**`, Joe's), so the run states the intensity it wants. Disclosed in the header and in PLAN.md's line. Rejected: non-finite or negative. |

**Disclosed, not enforced** (Joe's review, same shape as `docs/lessons.md`'s 2026-09-15 "two plugins,
two radii" entry): `w20_knots` and `sigma_high` are independently configured, but MIL-F-8785C derives
both from one underlying exceedance-probability chart, and the 1000–2000 ft band blends them via a
straight-line interpolation that assumes they are a consistent pair. Nothing here rejects a config
that names an inconsistent pair (e.g. a `w20_knots` implying light turbulence at low altitude paired
with a `sigma_high` implying severe turbulence aloft) — there is no golden chart in the tree to check
consistency against, the same reason `sigma_high` itself is a free parameter. Stated in the header,
not fixed.

**Turbulence only, zero mean — decided, and disclosed.** `dryden` does not take `north`/`east`/`down`
steady-wind keys. `W_20` is an intensity parameter, not a mean-wind command, and folding a steady
component in here would pre-empt the wind-composition design (a summing/layered `WindModel`) that
PR 3's steady-wind NESC case is the right place to decide. A config needing both today uses
`wind/uniform`.

**Per-frame path**, matching `wind_model.hpp`'s three-way split exactly:

- `step(ctx)` — draws exactly **five** `normal()` samples into a fixed member array and does nothing
  else. Constant count, scheduler-driven, independent of how many vehicles or consumers exist (§2
  principle 2). `physical.wind`'s divisor is already forced to 1 by `Simulation::from_config`.
- `begin_frame(ctx, vs, altitude_m)` — `V = vs.v_b().norm()`, `h = altitude_m`; evaluates the MIL
  parameters, the exact discrete coefficients, and advances (or, on the first frame, stationary-
  initializes) the three filters from the five drawn samples; forms the gust in **body axes**
  `(u_g, v_g, w_g)` and latches `wind_n_ = frames::rotate_b_to_n(vs.q_nb(), gust_b)`.
  Also updates the published `sigma_*` / `length_*` / `airspeed` / `altitude` members.
- `wind_n(ctx, vs)` — `const noexcept`, returns `wind_n_`. A pure read; it recomputes nothing.
- `reset()` — reseeds the `Rng` to the configured seed (clearing the Box-Muller cache), zeros the
  filter states and `wind_n_`, and re-arms the stationary-start flag.
- `shareable_across_vehicles()` — **not overridden**: the default `false` ships, per
  `wind_model.hpp`'s own comment. The filter state is keyed to one vehicle's `V` and `h`, so it
  cannot honor two at once; `Simulation::from_config` already rejects >1 vehicle with a
  non-shareable wind model. A test asserts it is `false` so a future accidental override is caught.
- `assumed_position_representation()` — not overridden (`kAny`): `dryden` reads `v_b` and `q_nb`
  only, and takes altitude from the Earth-derived `altitude_m` parameter, never `vs.p_n()`.

**Two sign/frame statements the header must make explicitly** (the adversarial-review focus):

1. `wind_n` is **wind-to**, in NED (`wind_model.hpp`, ADR-0001); `(u_g, v_g, w_g)` are treated as the
   air-mass velocity components along body FRD axes, so `wind_n = rotate_b_to_n(q_nb, gust_b)` and a
   consumer's `v_b - rotate_n_to_b(q_nb, wind_n)` is the air-relative velocity. A PSD is
   sign-blind, so no test can catch this being backwards — hence it is stated, not implied.
2. The `V` the filter uses is **Earth-relative** `|v_b|`, not air-relative. Reason, stated in the
   header: the gust this filter is computing is precisely what turns Earth-relative velocity into
   air-relative velocity, so using air-relative airspeed would close an algebraic loop through the
   model's own output. It is the standard MIL-F-8785C implementation simplification, and its error is
   `O(sigma/V)`; unstated it reads as a bug.

**Signals** (`registry.add`, unit as metadata per ADR-0002, no unit suffixes in the names): the
`Rng` seed as `wind.seed` ("1", §15's "the seed is logged"), `wind.north`/`east`/`down` ("m/s", the
same three names `uniform` publishes), `wind.w20` ("m/s", the SI value actually used),
`wind.sigma_u`/`_v`/`_w` ("m/s"), `wind.length_u`/`_v`/`_w` ("m"), `wind.airspeed` ("m/s"),
`wind.altitude` ("m").

### B5. Tests

- `tests/unit/wind/wind_determinism_test.cpp` (**extend, reusing PR 2's harness as PLAN.md
  directs**): convert the three existing tests to value-parameterized tests over two configs —
  the existing `wind/uniform` one and a new `wind/dryden` one (`earth/flat`, initial
  `p_n = (0,0,-100)` so `altitude = 100 m`, `v_b = (60,0,0)`). All three assertions (per-frame
  `wind.*` sequence, every registered signal, the final state vector) must be bitwise identical
  across two independently constructed `Simulation`s. Against `dryden` a mismatch is diagnostic of a
  real RNG/filter bug rather than trivially passing.
- `tests/unit/wind/dryden_test.cpp` (new): plugin loaded directly through `PluginLoader` at
  `ASTRA_TEST_PLUGIN_DIR`, driven with a synthetic `VehicleState`.
  - **`reset()` acceptance**: step 200 frames, `reset()`, step 200 more → bitwise identical to a
    freshly loaded model's first 200 frames, on every gust component.
  - A different `seed` produces a different sequence (the seed is actually used).
  - `shareable_across_vehicles() == false`; `assumed_position_representation() == kAny`.
  - Config rejections, each asserting `ConfigError::config_path()`: missing `seed` →
    `physical.wind.seed`; negative seed; seed `> 2^53`; missing / non-finite / negative
    `w20_knots`; missing / non-finite / negative `sigma_high`.
  - Gusts stay finite over 10 000 frames at `V = 0` (the `a == 0` limit) and at the 10 ft altitude
    clamp, including `altitude_m < 0`.
- `tests/physics/dryden_psd_test.cpp` (new, `physics` label — skipped by the Stop hook's `ctest -L
  unit`, run by `just check`): **the M2 acceptance**. Drives the loaded plugin's
  `step`/`begin_frame`/`wind_n` directly with a **synthetic, held-constant `VehicleState`** (PLAN.md
  settles this: MIL-F-8785C's PSD is a stationary-process spectrum, meaningful only at fixed `V` and
  `h`), `q_nb = identity` so the NED components are exactly `(u_g, v_g, w_g)`. No `Simulation`, no
  Earth model — `altitude_m` is passed directly.
  - Welch estimate with the scaling pinned and derived in a comment:
    `P(omega) = (dt / (pi*N)) * |sum_k x_k * exp(-i*omega*k*dt)|^2`, averaged over `K` segments,
    which estimates the same one-sided `Phi(omega)` MIL-F-8785C tabulates. Evaluated by direct
    summation at ~10 log-spaced frequencies per segment (no FFT dependency, and none is in the
    apt-only toolchain); cost is `O(K*N*10)` trig calls.
  - **Grid bounded on both ends, stated why (Joe's review)**: the exact discrete-time step
    reproduces the continuous filter's autocorrelation only *at the sample instants*, so the discrete
    process's spectrum is the continuous spectrum **aliased** to `[-pi/dt, pi/dt]`, not the raw
    continuous `Phi(omega)` itself. `Phi_u` and `Phi_w` fall as `omega^-2` at high frequency, so
    near Nyquist (`omega -> pi/dt`) the aliased sum of the spectrum's images sits *deterministically*
    above the un-aliased MIL formula — that is not estimator noise and no amount of averaging removes
    it. The comment derives and states the two grid bounds explicitly: the top of the grid is capped
    at a stated fraction of Nyquist (e.g. `omega_max = 0.1 * pi/dt`, chosen so the nearest alias image
    contributes below the statistical tolerance — checked numerically during implementation, not
    guessed), and the bottom of the grid stays at several multiples of `2*pi/T_segment` (the segment's
    own frequency resolution), since a bin near `omega -> 0` is dominated by leakage from the segment
    window, not signal. **If a frequency near either edge fails, the fix is more segments (`K`) or a
    tighter grid bound, never widening the tolerance** — AGENTS.md: narrowing/widening a check is a
    blocking review finding regardless of which test file it's in.
  - **Estimator self-check first**: run the same estimator over the `Rng`'s raw `normal()` output and
    assert `P ≈ dt/pi` flat across the grid (verified: white noise's one-sided PSD in this
    convention is exactly `dt/pi`, independent of frequency). This validates the estimator
    independently of Dryden, so a failure on the Dryden rows is unambiguous.
  - Then, for each of `u`, `v`, `w`: the estimate at each in-band frequency within the stated
    `chi^2_{2K}` bound (`~3.5/sqrt(K)` relative, derived in the comment from the sampling
    distribution, **not** fitted to the observed numbers), and the sample variance within the stated
    bound of `sigma_u^2`, `sigma_v^2`, `sigma_w^2`. Fixed seed, so the result is reproducible.
  - Sized for the gate: low altitude and high airspeed to keep `L/V` short (`h = 50 ft`,
    `V = 100 m/s` → `T_u ≈ 0.95 s`, `T_w ≈ 0.15 s`), `K ≈ 50` segments of ~20 s → ~1e6 samples.
    Target ≤ ~5 s release / ≤ ~30 s asan; tuned during implementation (more segments, not a looser
    bound or a wider grid) if it overruns or the near-Nyquist band fails.
- `tests/configs/no_alloc_dryden.yaml` + a second case in `tests/physics/no_alloc_test.cpp`: zero
  heap allocations over 1000 frames with `physical.wind` = `dryden` configured — §14's per-frame
  rule, and the adversarial-review focus.

### B6. Files touched

New: `core/include/astra/util/units.hpp`, `core/include/astra/util/random.hpp`,
`plugins/physical/wind/dryden/{CMakeLists.txt, dryden.cpp, dryden_filter.hpp}`,
`tests/golden/wind/mil_f_8785c_dryden.hpp` (**guarded, `tests/golden/**`** — see B2.5 and the note
below), `tests/unit/util/{units_test.cpp, random_test.cpp}`,
`tests/unit/wind/{dryden_test.cpp, dryden_filter_test.cpp}`,
`tests/physics/dryden_psd_test.cpp`, `tests/configs/no_alloc_dryden.yaml`.
Modified: `tests/unit/CMakeLists.txt`, `tests/physics/CMakeLists.txt`,
`tests/unit/wind/wind_determinism_test.cpp`, `tests/physics/no_alloc_test.cpp`, `PLAN.md`,
`docs/lessons.md` (if anything bites).
No core interface change, so **`kCoreApiVersion` is not bumped** (it stays 3): `dryden` implements
the `WindModel` vtable PR 2 already settled.

---

## Verification

1. `just check` run once, after the work, with the Bash tool's own `run_in_background` (never
   `nohup … &`, never any other shell-level backgrounding stacked on top of it — `docs/lessons.md`
   2026-09-15/09-15b). Then, per `docs/lessons.md` 2026-09-16 (`pgrep -x just` can return a decoy
   pid — a leftover `just` from an unrelated earlier call, or `ps aux | grep "just check"` matching
   the harness's own `bash -c 'eval ... just check ...'` wrapper line): find the pid by cross-checking
   `ps aux`'s full command column for the line whose command is **exactly** `just check`, excluding
   any `bash -c` wrapper; re-verify that pid is still alive immediately before blocking on it with
   `tail --pid=<that pid> -f /dev/null`; then read the log's `EXIT=0` / `check: OK` line. Nothing is
   verified before that line has been read. Every preset (`debug`, `release`, `asan`,
   `static-plugins`) plus clang-tidy, clang-format, hooks, ruff, pyright, pytest. No tracked
   C++/Python file is edited while a run is in flight (2026-09-15).
2. Acceptance, restated from PLAN.md's PR 2b line:
   - `physics.DrydenPsd.*` — PSD matches MIL-F-8785C within the stated statistical bounds at a fixed
     seed, per axis, plus the variance check and the estimator self-check.
   - `WindDeterminism/*` parameterized over `dryden` — two same-seed runs bitwise identical on the
     gust sequence, on every registered signal, and on the final state vector.
   - `Dryden.ResetReproducesTheSequence` — bitwise identical across `reset()`.
   - `NoAlloc.*Dryden*` — zero allocations over 1000 frames.
3. Before the PR, in order:
   - `git add -A && git commit` for the docs commit, then for the Dryden commit (Codex's `git diff`
     never shows untracked files, and this PR is mostly new files).
   - `node "$(find ~/.claude/plugins -name codex-companion.mjs | head -1)" review --base origin/main
     --wait`
   - `… adversarial-review --base origin/main --wait` with a focus naming ADR-0001 (the wind-to sign
     and the n frame), ADR-0002 (SI, the ft/kt conversions, unit as signal metadata), ADR-0009, §2
     principle 2 (seeded, scheduler-stepped randomness — `step()` draws, `begin_frame` advances,
     `wind_n` is a pure const read), §15's determinism guard, and §14 (no per-frame allocation or
     exceptions).
   - Push the branch, open the PR myself. This is a physics diff, so Joe reads it.
4. End with `/wrap`.
