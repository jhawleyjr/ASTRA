# ADR-0031: The aero envelope declaration and its health check

Status: proposed
Date: 2026-10-04
Owner: Joe
Related/supersedes: supersedes **one sentence** of ADR-0020's Decision, "`AeroModel`/`PropulsionModel` become
minimal marker interfaces, with exactly one deliberate, non-empty override each", **for `AeroModel` only**:
`AeroModel` gains a second defaulted virtual. ADR-0020 is not edited. Its own text names "any future
envelope-monitor hook" among the reasons the marker types exist, and `PropulsionModel` is unchanged.

This record elaborates three others:
- ADR-0026: the check reads its derived signals and adds no second derivation;
- ADR-0025: new counters and signals in the run log and its summary;
- ADR-0002 and ADR-0030: SI values and unsuffixed signal names.

Drafted with `docs/plans/m7-f16-nesc-oracle.md` (Q16, R18, amendment 8, N-07) and `docs/plans/m7-pr1a-adrs.md`
(Joe's D3 to D6 and amendments A4 to A6). Governs PLAN.md's M7 PR 13.

## Context

Architecture §15 has listed "envelope monitors from the aero model's declared limits: α, β, Mach, dynamic
pressure, load factor (`warn` by default)" among its per-frame checks. Its pre-run list also had "airspeed inside the aero
model's declared envelope", halt by default. Neither was built, because `AeroModel` declares nothing
(PLAN.md's (M7) item, origin M3 PR 3).

What makes it worth building now:
- **The F-16.** Its aero model is a set of tables with finite breakpoints. A run that leaves them still gives a
  plausible-looking trajectory, which §15 calls worse than a refusal.
- **NESC case 12.** It is flown at Mach about 2, by every participant, with the same model.
- **M6's R9 batch.** It plants a cause in the notional transport and judges outcomes by
  `max.v0.derived.alpha > 0.23`. Joe set the notional transport's limit at 0.23 rad (2026-10-03), the CL peak
  of its source (`docs/refs/aircrafts/737.xml`, CL 1.20 at 0.23 rad).

Facts the design rests on, observed at `9772031`:
- **ADR-0026's derived signals.** They publish `v<i>.derived.alpha`, `beta`, `mach`, `tas`, `az` and
  `status_flags` at the frame-start point, after the environment sample and before the exchange, through the
  one `pack_sim_to_sw` call.
  - `az` is frame k−1's specific force, by ADR-0026's timing exception, and `ACCEL_INVALID` is set at frame 0.
  - A field a status bit invalidates holds the defined 0, never NaN.
- **Dynamic pressure is in no log.** No atmosphere signal is registered, and the derived set has no density.
- **Health.** `HealthMonitor`'s checks end at `kLog`. Its actions are `off`, `warn` and `halt`, read from
  `sim.health.<name>`, and its counters are global signals `health.<name>`.
  - `_runkit.WARN_COUNTERS` and `HEALTH_SIGNALS` fix the counters `astra run --strict`, scenarios and reports
    know.
  - `astra run` raises on a `health.*` signal it does not know.

## Decision

1. **The declaration.** `AeroModel` gains one virtual, `envelope() const noexcept`, with a default. It returns
   five closed intervals, one per quantity:
   - angle of attack α (rad);
   - sideslip β (rad);
   - Mach (dimensionless);
   - dynamic pressure q̄ (Pa);
   - normal load factor n_z (dimensionless, +1 in level flight).

   How the intervals behave:
   - Either bound of an interval may be −∞ or +∞, meaning undeclared. The default declares nothing.
   - The core queries it once, after the model's `init()`, so a declaration may depend on config. It is never
     called on the per-frame path.
   - A NaN bound, or a lower bound above the upper, is a `ConfigError` naming the plugin's path.
   - A vehicle with several aero models checks each declaration on its own, and each check names its own
     plugin.
   - Lateral load factor is not declared. `PropulsionModel` declares nothing.
2. **What a model declares.** An aero model declares the domain of its own data. That means its table
   breakpoints, or, along an axis its data does not span, a validity limit stated by its source. It is not
   the airframe's structural or flight-manual envelope. This record sets no limit for any particular model.
   The notional transport's α ≤ 0.23 rad (Context) is the one example decided.
3. **What the check reads.** It runs once per vehicle per frame, at the frame-start point right after ADR-0026's
   derived stage, on that frame's values:
   - α, β and Mach: the derived signals themselves, with no second derivation;
   - q̄ = ½·ρ·tas², with ρ from the same frame-start environment sample the derived stage used;
   - n_z = −az / g0, with g0 = 9.80665 m/s², standard gravity, a named `core/util/units` constant.
4. **Skips.** A quantity whose derived inputs a status bit marks invalid is skipped that frame. It is never
   compared as the placeholder zero. Every skip maps to a bit already logged in `v<i>.derived.status_flags`,
   and there is no other skip:

   | Status bit | Skips |
   |---|---|
   | `WIND_INVALID` | α, β, Mach, q̄ |
   | `ATMOSPHERE_INVALID` | Mach, q̄ |
   | `ACCEL_INVALID` (frame 0 included) | n_z |

   - **What is checked.** Only a quantity with at least one finite bound is checked. A quantity with no
     declared bound is never counted, NaN included.
   - **Density.** `ATMOSPHERE_INVALID` tests pressure and speed of sound only (`icd_pack::pack_sim_to_sw`).
     Density is not a skip:
     - q̄ is computed from whatever ρ the sample holds;
     - a non-finite q̄ counts as outside a declared q̄ interval, and the published q̄ carries the NaN for that
       frame;
     - a non-finite ρ is already caught by `Vehicle`'s environment check (`core/src/kernel/vehicle.cpp`). That
       check tests density's finiteness and reports `kNonFiniteEnvironment` under `sim.health.nan`, which
       halts by default and is counted in `health.nan`.

5. **Timing.** n_z inherits `az`'s previous-frame timing, so the check sees a load-factor excursion one frame
   late, and a load-factor halt lands one frame after the excursion. The other four quantities describe the
   frame they are checked in. A halt stops the frame before the exchange and any `step()`, and the clock does
   not advance.
6. **One check, five actions and counters.**
   - **The check.** One `HealthCheck::kEnvelope` value is appended after `kLog`, so every existing value keeps
     its integer. A halt's source names the plugin and the quantity (for example
     `<aero plugin path>: mach`). The source is built at init.
   - **The actions.** Five keys under `sim.health.envelope`: `alpha`, `beta`, `mach`, `qbar` and
     `load_factor`. Each takes `off`, `warn` or `halt`, and `warn` is the default. A config's loosening or
     tightening is recorded in the log, as every `sim.health` key is.
   - **The counters.** Five global counters, `health.envelope.alpha` and so on, always registered. Each
     counts the checks that found its quantity outside a declaration, as every existing counter does.
   - **Why one check and not five.** The halt already names the quantity in its source, and the action and
     the counter are per quantity anyway. Five enum values would add five Python enum names and five
     `/summary` spellings for one mechanism.
7. **Two published signals.** The check publishes the two quantities it computes itself, as per-vehicle
   signals with no unit suffix (ADR-0030): for example `v<i>.envelope.qbar` (Pa) and `v<i>.envelope.load_factor`
   (`"1"`). PR 13 sets the final names.
   - They are published for every vehicle of a config with physics, whether or not a model declares an
     envelope, so a log's columns do not depend on the declarations.
   - A skipped q̄ or n_z publishes 0, ADR-0026's convention. A reader decodes such a 0 from the same row's
     `v<i>.derived.status_flags`, through item 4's table. There is no separate envelope status signal.
   - With α, β and Mach already derived, every quantity a counter counts is then in the log and in the
     summary's min/max.
8. **No separate init check.** §15's pre-run "airspeed inside the aero model's declared envelope" is
   withdrawn. Frame 0's check, under each quantity's configured action, covers the initial state. A config
   that starts outside a declaration under `halt` halts at frame 0. Under `warn`, the counter records it.
9. **The tools.** The five counters join `_runkit.WARN_COUNTERS` and `HEALTH_SIGNALS`. `astra run --strict`
   exits 2 on an envelope-only warning (§15's exit table, unchanged). Scenarios run strict, and reports show
   the counters with the summary.

## Rejected alternatives

- **A declaration on every `WrenchSource`, propulsion included.** A thrust table's edge is also data the
  model does not have, but that is beyond Q16's scope. It can be revisited if the F-16's inventory (PLAN.md's
  M7 PR 6a) shows a propulsion edge that matters.
- **Limits from config only.** The model owns its data domain. A config can loosen the action, but not move
  the domain away from the data.
- **A separate init-time envelope check, halt by default.** It would refuse NESC case 12, which every
  participant flew at Mach about 2 with the same model. It also duplicates frame 0's check under a second,
  different action.
- **Five `HealthCheck` values.** Rejected for item 6's reason.
- **Computing α, β and Mach again in the monitor.** ADR-0026 has one derivation, and a second could disagree
  with the logged signals the check is judged by.
- **`halt` by default.** A plausible-looking run outside a model's data should be visible, which `warn` and the
  counter make it, and `--strict` makes it fail. A halt by default would end deliberate runs such as case 12.

## Consequences and validation

- **The core API.** `AeroModel` gains a virtual, so `kCoreApiVersion` is bumped, and a plugin built against
  the preceding version is rejected at load. PR 13 states the number.
- **The log.** Every log gains the five counters and each vehicle's two signals. Tests that pin a log's column
  set move, and PR 13 lists them before and after. The summary's minimum of `load_factor` includes frame 0's
  defined 0 (item 7), which is disclosed.
- **A non-positive density.** A defective atmosphere plugin can give a finite ρ ≤ 0. That gives q̄ ≤ 0, which
  passes when the q̄ lower bound is undeclared. Nothing refuses a non-positive density today, and fixing that
  is not this record's job. This is disclosed.
- **M6's R9.** Its batch is unchanged under `warn`: an envelope warning changes no status, and its criterion
  reads `v0.derived.alpha`, not the counter.
- **NESC case 12.** Its config states `sim.health.envelope.mach: warn` explicitly. The NESC harness gains the
  M7 plan's R18 warn-counter policy:
  - no halt, and every envelope counter zero, in every case;
  - except case 12, whose Mach counter must be nonzero while its other envelope counters stay zero.

  That expectation depends on the F-16 declaring a Mach limit. Whether P1 states one is an obligation the M7
  plan carries to PR 6a and PR 13, and it returns to Joe if P1 states none.
- **Acceptance (PR 13).**
  - `ctest --preset asan -R 'Health|Envelope|PluginLoader' --no-tests=error`: each quantity driven past a
    declared bound fires with its action and source; each skip bit skips; a quantity with no finite bound is
    never counted; a non-finite q̄ counts as outside a declared q̄ interval; the n_z lag is one frame; a stale
    plugin is rejected.
  - `.venv/bin/pytest tests/python -k 'planted or cli or scenario or report'`: R9 unchanged; `--strict` exits 2
    on an envelope-only warning; the counters are in the summary and the report.

## Approval

Proposed. Awaiting Joe's explicit acceptance.
