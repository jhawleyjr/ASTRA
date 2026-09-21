# Human review packet: M3 PR 4 — `aircraft/notional_transport/physical/aero` + `hardware/propulsion/turbofan`

PR / candidate base and head: base `origin/main` at `6590b7c` (PR #40, confirmed current tip this
session); head `8b77c24` on branch `worktree-m3-pr4-notional-transport`
(`fe61ab9` merge-bookkeeping → `fecdda8` plan approval → `08b4d2d` implementation → `2b157eb`
Codex general-review fixes → `9334637` STATE.md → `f3cae77` this packet's first draft → `990864a`
**Joe's own review**: fixed a real One Definition Rule hazard → `8b77c24` Codex fix-verification's
own follow-up finding closed). Working tree clean at head; no dirty-tree scope.
Spec/plan: `docs/plans/pr4-notional-aero-turbofan.md` (Revision 2, **approved by Joe, 2026-09-20**,
conditioned on four edits — all applied in Revision 2 and shown to Joe before implementation began).
Risk: **Tier 3** (`aircraft/**/physical`, `aircraft/**/hardware`, per `docs/process/workflow.md`'s
risk table).
Decision requested: **approve behavior and merge.** (An earlier draft of this packet requested
authorization for one more fix, since the workflow's two-round autonomous correction cap had been
reached with one finding open; Joe granted that fix directly, out-of-band from the cap, and it is
now closed — see "Independent review" and "Decision record.")

## What changes and why

Adds the only two concrete `WrenchSource` plugins `aircraft/notional_transport` needs for PLAN.md's
M3 trim/linearize/regression milestone (PR 6/7/8): a linear-coefficient `AeroModel`
(`physical/aero`) and a linear thrust-lapse `PropulsionModel` (`hardware/propulsion/turbofan`, two
station instances), both overriding `WrenchSource::wrench_b()` directly per ADR-0020 — no `compute()`
step, no core-defined `Inputs` struct. Numbers are notional, transcribed/derived from JSBSim's "737"
example aircraft (`docs/refs/aircrafts/737.xml`, gitignored), the same source PR 1 already used for
mass properties.

**New pattern this PR establishes, not just numbers**: each plugin gets a sibling `aero_math.hpp`/
`turbofan_math.hpp` — a plugin-local `Inputs` struct, a plugin-local `Coefficients` struct, and a
pure, free `compute_wrench()` with no `Config`/`VehicleState`/`EnvironmentSample`/I-O dependency —
so the math is unit-tested with no simulator, mirroring the existing
`plugins/physical/wind/dryden/dryden_filter.hpp` precedent exactly. `aircraft/_template/` ships the
identical pattern (generic, TODO-zeroed), so a future aircraft starts from the same shape. This
convention was independently critiqued by Codex at the plan stage (see "Independent review") since
every future aircraft's own aero/propulsion plugin will likely copy whatever shape this PR ships.

**Before**: `aircraft/notional_transport/configs/cruise.yaml`'s `vehicle.models` was empty (the
vehicle free-falls under gravity); `aircraft/_template/`'s own config had no aero/turbofan entries at
all. **After**: both configs carry real (`notional_transport`) or TODO-zeroed (`_template`)
`vehicle.models` entries plus a `vehicle.initial_controls` block; `notional_transport`'s own
1000-frame run flies, un-trimmed (PR 6 still does the real trim), under real aero+thrust instead of
free-falling. `docs/architecture.md` §13's config excerpt and §7's status prose are updated to match
current reality (§7's prose was already stale before this PR, describing PR 3d as "not yet
implemented" after it had, in fact, merged — fixed incidentally while updating the adjacent text).

Diff stat (excluding the two retained full-gate logs under `docs/reviews/evidence/`, ~500 KB of
plain-text `ctest`/`clang-tidy` output each, not code): 36 files changed, 2785 insertions(+), 324
deletions(-) — plugin source/CMake files × 2 aircraft copies, the ODR-regression fixture plugin,
9 new test files, `scripts/new-aircraft.sh`'s namespace rewrite, 2 `cruise.yaml`, 2 `docs/*.md`, the
plan doc itself, `cmake/AstraPlugin.cmake`, 2 test `CMakeLists.txt`, 2 existing test files updated
for the new real config, `PLAN.md`, `STATE.md`, this packet.

**The consequential end-to-end path**: `aircraft/notional_transport/configs/cruise.yaml` →
`Simulation::from_config` → `Vehicle::init` discovers both `WrenchSource`s, calls `bind_inputs()`
(registering `controls.{elevator,aileron,rudder,throttle}`) → `vehicle.initial_controls` writes the
config-literal values → every frame, `Vehicle::integrate()`'s per-RK4-stage derivative functor calls
`wrench_b()` on each source, which assembles this plugin's own `Inputs` via `air_data::` utilities
and this plugin's bound controls, checks finiteness, and delegates to `compute_wrench()` — exercised
directly by `tests/unit/kernel/notional_transport_cruise_config_test.cpp` (1000 frames, no halt) and
`tests/physics/no_alloc_test.cpp`'s new case (same run, zero heap allocations).

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| `./scripts/check-full.sh` (`just check`), on `2b157eb` | **`check: OK`**, exit 0 | `/tmp/check-full-pr4-3.log` (session-local; superseded by the retained run below) |
| `./scripts/check-full.sh`, on `990864a` (superseded), from-scratch build | **`check: OK`**, exit 0 | Log deleted, Joe's own instruction: `8b77c24` is `990864a` plus a docs-only follow-up (§18 wording + this deletion), so `8b77c24`'s own gate run below already covers everything `990864a`'s did |
| `./scripts/check-full.sh`, on `8b77c24` (final code candidate), from-scratch build | **`check: OK`**, exit 0 | `docs/reviews/evidence/8b77c24-full-gate.log` |
| `just docs-check`, on the final candidate (docs-only diff on top of `8b77c24`, no code change, no full-gate rerun needed) | **`docs-check: OK`** (24 Markdown files checked) | session output |
| Four presets × unit+physics tests (`8b77c24`) | **362/362 each** (debug/release/static-plugins/asan) | `8b77c24` log |
| clang-format | 0 violations | same log |
| clang-tidy (`build/asan` + `build/python`) | 0 findings (`bugprone-reserved-identifier` caught a double-underscore namespace name mid-session — fixed before this log) | same log |
| pyright (strict, `python/astra/`) | 0 errors, 0 warnings, 0 informations | same log |
| pytest | **41/41 passed** (3 new cases since the first packet draft: two ODR-namespace regressions, one `ruff` line-length fix) | same log |
| `docs-check` / hooks-test / ruff | OK / 144 passed / OK | same log |
| Plan-stage: Condition N/O/Z + symmetric-yaw literals independently computed | Reproduced by a fresh Python re-implementation (not this plugin's own code), then reproduced again independently by Codex's own scalar calculation during adversarial review | `docs/plans/pr4-notional-aero-turbofan.md`'s own "Worked numbers"; adversarial-review transcript |
| Build-time precision bug found and fixed | `wing_area_m2` transcribed as `108.789460` (6-decimal rounding) vs. the exact `1171 × 0.09290304 = 108.78945984`; failed the plan's own 1e-9 relative tolerance by ~1e-5 absolute until corrected | Caught by the first `ctest` run against the real build, not by inspection; fixed in `08b4d2d` before that commit closed |
| ODR-hazard empirically confirmed, not just argued | Reintroduced the original shared-namespace bug by hand, rebuilt `static-plugins` from scratch: compiled and linked with **no error or warning**, then **crashed under ASan** (stack corruption) at runtime — confirms the linker really does silently pick one definition, exactly as Joe described | Session transcript; not separately logged (destructive local experiment, reverted before committing) |

Toolchain: Ubuntu 24.04, clang-18, the pinned dev-group Python venv — unchanged from PR 3d's own
environment. No new dependency. No `tests/golden/**` touch, no ADR change — this PR's own literal
Condition N/O/Z values (in the plan, transcribed into the tests) are the independent oracle; no
separate human-owned-oracle authorization was needed per the plan's own "Validation and risks"
section, and none was requested.

## Independent review

Builder: Claude (Sonnet 5). Reviewer: Codex (`codex-companion` 1.0.6), fresh context throughout.

**Plan-stage critique** (scoped to the new `_math.hpp` shape specifically, per
`docs/process/workflow.md`'s "unpinned design expensive to unwind" trigger — dispatched because this
is the first concrete instance of a convention ADR-0020 only sketched): verdict "retain the
pattern," two findings, both accepted and resolved before implementation began — (1) `Inputs`
included an unused `mach` field (this plugin's coefficients are constants, never Mach-scheduled;
dropping it was also a disclosed deviation from Joe's own original instruction to use `air_data::`
for Mach, which he then accepted); (2) the turbofan `wrench_b()`-level acceptance only exercised a
condition whose density coincidentally equaled the config's own reference density, so a
config/sampled-density wiring bug would have passed undetected — a second condition with a
differing density was added at both the pure-math and `wrench_b()` levels.

**Implementation-stage general review** (scope: committed diff, base `6590b7c`, candidate `08b4d2d`):
two findings, both **major/P2**, both fixed in `2b157eb` —
- `aero.cpp`/`turbofan.cpp` included their own `_math.hpp` by the full aircraft-specific path. Since
  `just new-aircraft` only rewrites YAML paths, not `.cpp` string literals, a copied aircraft's own
  edited `aero_math.hpp` would be silently ignored — the compiled plugin would keep including
  `_template`'s header. Fixed: sibling-relative includes (`#include "aero_math.hpp"`), matching
  `plugins/physical/wind/dryden/dryden.cpp`'s own existing precedent exactly (verified by reading
  that file directly, not assumed).
- `checked_positive()`'s `value > 0.0` check is true for `+Inf`, so an infinite
  `thrust_ref_n`/`density_ref_kgm3`/`wing_area_m2`/etc. passed init-time validation; the turbofan
  thrust law then divided a finite density by an infinite `density_ref_kgm3` and silently returned
  zero thrust rather than failing loudly — the exact "plausible-looking wrong answer" this project's
  own health-monitoring philosophy exists to prevent, just at config-validation time instead of
  runtime. Fixed: `checked_positive()` now also requires `std::isfinite()`, in both plugins' both
  aircraft copies; two new regression tests
  (`TemplateAero.InfiniteWingAreaThrowsConfigError`,
  `TemplateTurbofan.InfiniteDensityRefThrowsConfigErrorInsteadOfSilentlyZeroingThrust`).

**Implementation-stage adversarial review** (scope: same diff, focus: ADR-0001 frame/sign
conventions — especially the plan's own JSBSim-SIDE-vs-S&L-`-C` side-force fix — ADR-0002 SI units,
the V→0 division-free algebra in both `_math.hpp` headers, ADR-0016 per-RK4-substage purity, per-frame
allocation, the R-001-shaped finiteness check, whether the test literals are genuinely independent of
the plugin's own code, and `_template`/real-aircraft byte-parity): **verdict approve, no material
findings**. Independently re-derived the rotation-matrix/rate-damping/thrust-law arithmetic by a
separate scalar calculation and confirmed it reproduces the plan's own literals.

**Round 2 — Joe's own direct review of `2b157eb`** found one **major** finding neither Codex pass
above had caught: `aircraft/notional_transport`'s and `aircraft/_template`'s `aero_math.hpp`/
`turbofan_math.hpp` both declared their `Inputs`/`Coefficients`/`compute_wrench()` in the **same**
generic C++ namespace (`astra::linear_aero`, `astra::thrust_lapse`). Under `ASTRA_STATIC_PLUGINS`
every plugin's object links into one binary with no per-plugin visibility boundary (unlike the
shared-object presets' own `-fvisibility=hidden`), so the moment a copied aircraft's own math header
is edited to differ from `_template`'s, this is a silent One Definition Rule violation — the linker
keeps one definition for both. **Empirically confirmed, not just argued** (see "Evidence"): forcing
the collision by hand compiles and links `static-plugins` cleanly, then crashes under ASan at
runtime. Fixed in `990864a`: each aircraft's math header now lives in its own namespace
(`astra::aircraft_notional_transport_{aero,turbofan}_math`,
`astra::aircraft_template_{aero,turbofan}_math`), `scripts/new-aircraft.sh` rewrites the token on
copy, and a new regression (`tests/plugins/aero_math_odr_check` +
`tests/unit/aero/aero_math_namespace_isolation_test.cpp`) loads the real plugin and a
deliberately-different fixture in one process and checks both produce distinct, correct answers —
runs under all four presets, static-plugins included, no special targeting. Also fixed in the same
round: `turbofan.cpp`'s `name()` returned the bare `"turbofan"` for both engine instances (now
`"turbofan.<station>"`, useless-for-diagnosis names was Joe's own point); a comment now documents
that `controls.throttle` is not range-checked against `[0, 1]`.

**Codex fix-verification of `990864a`** found the fix itself had a gap: naming a new aircraft
literally `template` (no leading underscore, distinct from the already-rejected `_template`) made
`new-aircraft.sh`'s namespace-rewrite `sed` a no-op, silently recreating the exact collision with
`_template`'s own namespace. Fixed in `8b77c24`: `template` is now rejected outright; a new
pairwise-uniqueness pytest test generates two real aircraft via the actual script and checks all
three namespaces (both plus `_template`'s own) are pairwise distinct.

**Codex's second fix-verification pass, against `8b77c24`** found one more finding, medium
severity: `docs/architecture.md` §18 step 1 still documented plain `cp -r aircraft/_template
aircraft/my_aircraft` as equally valid to `just new-aircraft my_aircraft`, describing the latter as
merely "the same [but] fixes the config's paths" — inaccurate once the namespace rewrite existed,
since only `just new-aircraft` performs it. Codex's own judgment on the prior two script-level
fixes (rejecting `template`, the pairwise-uniqueness test), requested explicitly: a **proportionate
response** to its own earlier "the fixture is disconnected from the real generation mechanism"
concern — a full compiled-and-linked integration test of a dynamically generated aircraft was
judged unnecessary.

This finding was reached after the session's own two-round autonomous correction cap, so it was
reported to Joe rather than fixed autonomously. **Joe explicitly authorized one bounded fix for it
(does not reset the two-round cap)**: §18 step 1 now documents `just new-aircraft` as the only
route, with the `cp -r` alternative removed and a clause stating why (it rewrites both the config
paths and the math-header namespaces); the superseded `990864a-full-gate.log` was deleted (its own
gate run is fully subsumed by `8b77c24`'s); `just docs-check` passed; Codex verified the delta (one
`P2` finding — the evidence-table/`STATE.md` references to the just-deleted log — fixed in the same
pass, see "Evidence"). **Closed.**

## Joe's attention

- **Follow-up, not blocking**: nothing else identified this round. The `cp -r`/namespace gap above
  is closed; §18 now names one route only.
- **The side-force sign fix** (`aircraft/notional_transport/physical/aero/aero_math.hpp`'s own header
  comment and the plan's "Force/moment assembly" section): this plugin's wind-axis force vector is
  `(-D, Y, -L)` with `Y = cy_beta_per_rad * beta * qbar * S` (JSBSim's own native +right `SIDE`
  convention, used directly), **not** S&L's literal `(-D, -C, -L)` — substituting `CYb` for S&L's own
  `C_C` and negating it would have flipped the physical sense. This was Joe's own catch during plan
  review, not something the implementation independently re-derived; worth a direct look given it's
  exactly the kind of sign error that "looks like physics" (ADR-0001's own stated risk).
- **A real, pre-existing framework documentation gap surfaced by this PR**: neither ADR-0001 nor the
  ICD (`icd/astra_icd.h:45`) actually states what "positive elevator/aileron/rudder" means physically
  — both only point at each other circularly. This PR is the first plugin that needed a definition
  and pins it via JSBSim's own convention (approved by Joe as the ICD convention this session); the
  framework-wide gap itself is now tracked in `PLAN.md`'s own follow-up list (added and shown to Joe
  as a diff this session), not resolved.
- **Disclosed gaps, labeled not silently dropped** (unchanged from the plan): no `Cmadot` term (no
  `alpha_dot` snapshot exists anywhere until ADR-0021 lands); no envelope monitors (already deferred,
  still deferred); no engine-out/asymmetric-thrust modeling; the notional thrust magnitude is
  self-derived from this airframe's own aero coefficients, not a real CFM56 spec.
- **No core interface, ADR, or dependency change.** `kCoreApiVersion` does not bump. Rollback is a
  plain revert of this PR's own commits; nothing else in the tree depends on these two plugins yet
  (PR 6/7/8 do, but haven't started).

## Decision record

State: pending (all findings closed; awaiting Joe's merge decision)
Human decision/date/reference: **Joe explicitly authorized the final §18/evidence-log fix**,
2026-09-20 (does not count against the session's two-round autonomous correction cap); merge/PR
decision itself still pending
Approved candidate/scope: pending (proposed: this session's final commit on
`worktree-m3-pr4-notional-transport`, squash-merge to `main`)
Merge observed: not merged
Release authorization: pending / not requested

Significant tradeoff rationale: none beyond what the plan already recorded (linear-coefficient
aero over table interpolation, density-ratio-only thrust lapse over a spooled engine model — both
Joe's own explicit "keep it the simplest possible example" instruction, not a cost/benefit trade
made unilaterally here). The ODR-namespace fix's own scope (script + generation-time tests, not a
full compiled-integration test of a dynamically generated aircraft) is a second, smaller tradeoff,
made by Codex's own judgment during fix-verification and reported here, not decided unilaterally.
Accepted risks: none new beyond the plan's own disclosed gaps (Cmadot, envelope monitors,
engine-out — owner Joe, follow-up trigger PR 4a or a future PR).
Nonblocking follow-ups: `PLAN.md`'s `cmake/AstraPlugin.cmake` `astra_finalize_plugins()`
hardcoded-test-list follow-up (pre-existing, this PR added four more entries to the same list rather
than fixing the underlying pattern); the ADR-0001/ICD elevator/aileron/rudder sign-convention
documentation gap (tracked in `PLAN.md`, this session).
Architectural decisions: none introduced or depended on beyond the already-accepted ADR-0020.
