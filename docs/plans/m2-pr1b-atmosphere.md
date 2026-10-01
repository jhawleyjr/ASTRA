# M2 PR 1b — `AtmosphereModel` + `plugins/physical/atmosphere/isa1976`

Recovered from `~/.claude/plans/m2-pr-1b-plan-md-concurrent-wind.md` on 2026-09-30. Approval: the file records none; the work merged as #24. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

M2 PR 1 (#22) hit its size contingency and split: the Earth and gravity models landed, the atmosphere
did not. PR 1 left the sockets pre-wired and named, which is why this PR is mechanical rather than a
new design:

- `EarthModel::altitude(const VehicleState&)` exists on all three Earth plugins and **has no callers
  yet** — its own doc comment says it is "what a later atmosphere model … need[s]".
- `earth_model.hpp:41` and `simulation.cpp:79-81` already promise a position-representation check for
  `physical.atmosphere`. **That promise was wrong and this PR retracts it** — see Decision 3.
- `round.cpp:90` already names the API: "the atmosphere model's `valid_range()` is the authority on
  whether an altitude makes physical sense for a run".
- `simulation_physical_config_test.cpp:93` asserts `physical.atmosphere` is *rejected*, with a
  comment saying it exists only until this PR.

Outcome: `physical.atmosphere` becomes a real, optional config slot; ISA-1976 reproduces the
published table exactly; §15's pre-run "altitude inside the atmosphere model's valid range" check
becomes real.

**Plan review (`/codex:rescue`): skipped, per your conditional.** Recon confirms the altitude-from-`p_n`
handoff is fully settled by PR 1 — `EarthModel::altitude(VehicleState)` was built for exactly this
and nothing else calls it, so there is no position-access design left to decide here.

## The golden's source (Decision 1) — settled

Nothing local could supply it: no ISA reference values anywhere in the repo (`tests/golden/` holds
only the frames and NESC headers), and no venv package ships the 1976 tables (`ambiance`, `pyatmos`,
`fluids`, `aerosandbox` all absent; `numpy`/`scipy` do not carry them). Your call: **fetch and
transcribe the standard.**

Procedure, scoped as you specified — transcribe little, compute the rest:

1. `WebFetch` **NASA-TM-X-74335 (NTRS 19770009539)** and read it. **If it does not fetch, I stop and
   tell you** — no fallback to memory, no silent substitution of a secondary source.
2. **Transcribe, straight from the document** (eyeball-checkable):
   - the defining constants `g0'`, `r0`, `R*`, `M0`, `gamma`;
   - the seven-layer table: base altitude, base temperature, lapse rate;
   - the layer base pressures;
   - **printed table rows** at 0, 11, 20, 32, 47, 51, 71 and 86 km — `T`, `p`, `rho` — read directly
     off the document's own tables, independently of the layer/constant transcription above.
3. **Record which altitude column was read** — geopotential `H` (km′) vs geometric `Z` (km) — in the
   provenance block, per row of the layer table. This is the likeliest transcription error and the
   same class of conflation as MSL vs ellipsoid height.
4. **Compute the dense golden rows in Python** from those document-verified constants. **No ASTRA
   code in that script**: it is an independent implementation, and that independence is what catches
   a wrong exponent or a flipped lapse sign in the plugin.
5. **Cross-check before showing you**: the transcribed layer base pressures must match what the
   Python script gets by integrating upward from `101325 Pa`. **If they disagree, something is wrong
   in the constants and I say so — I do not reconcile it quietly.**
6. **Provenance block states**: document, edition, and page/table number for the constants and for
   the eight transcribed rows separately; which altitude column each was read from; and that the
   dense rows are *computed*, the eight table-point rows are *transcribed*, with the generator
   committed beside the golden as `tests/golden/atmosphere/generate_isa1976_table.py` (inside the
   guarded path, so it is yours too) and re-runnable to regenerate the computed header.
7. **You then read the constants, the base pressures, and the eight transcribed rows against the
   PDF.** Until that happens the guard does not count as lifted, whatever the marker file says.

## Decisions

1. **Golden source: transcribed from the 1976 standard, checked as above.**
2. **Base pressures are `inline const`, not `constexpr`, and integrate upward from sea level.**
   Probe run this session, `clang++-18 -std=c++23 -fsyntax-only`:
   `constexpr double kPow = std::pow(2.0, 3.0);` → *"non-constexpr function 'pow' cannot be used in a
   constant expression"*, same for `std::exp`; `EXIT=1`, 4 errors. You were right, and gcc's
   acceptance would not have saved the `-Werror` clang-18 build. Concretely: the eight-entry layer
   table is filled in `init()` (which may allocate and throw) into a `std::array<Layer, 8>` member,
   each layer's base pressure computed from **the layer below**, starting at the defining
   `p = 101325 Pa` at sea level and integrating upward. `at()` then only reads members and stays
   `noexcept`. This also avoids namespace-scope static-initialization entirely.
3. **No `AtmosphereModel::assumed_position_representation()`, and no
   `tests/plugins/geodetic_atmosphere` stub.** You are right that the check cannot fire:
   `at(double altitude_m)` hands the model a scalar, so no conforming `AtmosphereModel` can read
   `p_n` at all and no value of the enum but `kAny` is reachable. A check that no conforming model
   can trip is not a check. Four stale comments get amended in this PR instead:
   - `core/include/astra/interfaces/earth_model.hpp:41` — "reject a gravity **or atmosphere** model"
     → gravity only, with one line saying why the atmosphere cannot need it (`at()` takes an
     altitude, not `p_n`).
   - `core/include/astra/interfaces/position_representation.hpp` — its file comment ("a GravityModel
     **or AtmosphereModel** that reads `p_n` directly") and the `kAny` line's
     "e.g. gravity/constant, **atmosphere/isa1976**" example.
   - `core/src/kernel/simulation.cpp:79-81` — "the same check will apply to `physical.atmosphere`
     once that slot exists" → the slot now exists and deliberately does not take the check.
   - `PLAN.md`'s PR 1b line, which promises the extension.
4. **`AtmoState` members default to `std::numeric_limits<double>::quiet_NaN()`**, not `0.0` — a
   default-constructed `AtmoState` must not read as a silent `rho = 0`. Same reasoning as the
   out-of-range return below, and it makes the two consistent.
5. **The initial-state altitude check goes in `Simulation::from_config`** beside PR 1's
   `validate_p_n` calls, not in `HealthMonitor::init` — that class is the frame-loop observer, and
   its `init` only parses `sim.health` and registers signals.
6. **`physical.atmosphere` is optional**, using your comment text verbatim.
7. **`AtmoState` is the four-field struct**: `temperature_k`, `pressure_pa`, `density_kg_m3`,
   `speed_of_sound_m_s`.
8. **`at()` returns NaNs outside `valid_range()`; it does not clamp.** §7 makes it `const`, §14 makes
   it `noexcept`, and `AtmoState` carries no status field, so it cannot report out-of-range by
   throwing. Clamping would hand a consumer a plausible density for an altitude the model does not
   model. NaN reuses the mechanism `round.cpp` already relies on: the value reaches a wrench and
   `HealthMonitor`'s existing per-frame NaN/Inf check turns it into a named halt.

## Files

### New: the interface

`core/include/astra/interfaces/atmosphere_model.hpp`

```cpp
struct AtmoState {
  double temperature_k = std::numeric_limits<double>::quiet_NaN();
  double pressure_pa = std::numeric_limits<double>::quiet_NaN();
  double density_kg_m3 = std::numeric_limits<double>::quiet_NaN();
  double speed_of_sound_m_s = std::numeric_limits<double>::quiet_NaN();
};

// Closed interval of altitudes, metres above the Earth model's reference surface, over which at()
// returns meaningful values. Both bounds inclusive. Checked once at init against the initial state
// (§15); at() returns an all-NaN AtmoState outside it.
struct AltitudeRange {
  double min_m = 0.0;
  double max_m = 0.0;
};

class AtmosphereModel : public Model {
 public:
  ~AtmosphereModel() override;                 // key function, out of line — see earth_model.hpp
  [[nodiscard]] virtual AtmoState at(double altitude_m) const noexcept = 0;
  [[nodiscard]] virtual AltitudeRange valid_range() const noexcept = 0;
};
```

`valid_range()` is data, not a validator, so — unlike `GravityModel::validate_p_n` — there is no
per-plugin virtual check. PR 1 needed virtuals because each model's *singularity reasoning* differed;
here the declared range is the whole declaration, and the comparison lives once in core.

`core/src/interfaces/atmosphere_model.cpp` — the key function alone (with Decision 3 there are no
non-pure virtuals left), following `core/src/interfaces/gravity_model.cpp`'s shape.

`core/CMakeLists.txt` — add `src/interfaces/atmosphere_model.cpp` to `astra_core` (alphabetically
first among `src/interfaces/`).

### New: the plugin

`plugins/physical/atmosphere/isa1976/CMakeLists.txt` — one line,
`astra_add_plugin(SOURCES isa1976.cpp)`. Discovery is a recursive glob; nothing else needs editing.

`plugins/physical/atmosphere/isa1976/isa1976.cpp` — copies the
`plugins/physical/gravity/inverse_square/inverse_square.cpp` template exactly: anonymous namespace,
`final` class, `name()` returns the slot name `"atmosphere"`, free `create()`,
`constexpr kDescriptor{"physical", "atmosphere_isa1976", kCoreApiVersion, kBuiltWith, &create}`, one
`ASTRA_PLUGIN_EXPORT` line last.

Parameterless, like `gravity/wgs84_j2`: `init()` reads no config and registers no signal. A delta-ISA
offset would be a knob nothing in M2 uses and would muddy "table points exact"; it belongs with PR 3's
first real consumer if it is wanted.

Constants stay **private to the plugin**, not in `core/util/`. PR 1 promoted geodesy to `core/util/`
because two plugins needed it; ISA has one consumer, and `AGENTS.md` puts model knowledge under
`plugins/`.

Structure (the constants are the ones transcribed from NASA-TM-X-74335; the plugin's header comment
cites the document and points at the golden's provenance block rather than restating it):
- Defining constants and the layer table as file-scope `constexpr` — these are literals, so they stay
  `constexpr`; only the derived base pressures cannot. The layer table's altitude column is named in
  the identifier itself (`kLayerBaseGeopotentialM`, not `kLayerBaseAltitudeM`), so the geopotential/
  geometric conflation cannot survive a reading of the code.
- `init()` walks the table upward from the defining sea-level pressure, filling each layer's base
  pressure from the layer below using that layer's own isothermal or gradient form.
- `at()` converts geometric → geopotential, binary-searches the layer, evaluates `T`, `p`, then
  `rho` and `a`, and returns all-NaN outside `valid_range()`. `noexcept`, no allocation, no branching
  on anything but the altitude.
- `valid_range()` returns the span the source tabulates.

### Changed: `core/src/kernel/simulation.cpp`

Inside the existing `if (config.has("physical"))` block, after the gravity model is scheduled:

- Replace the "all three slots or none" comment with your text (three required slots are the Eom's
  own inputs; atmosphere and, from PR 2, wind are optional; a wrench source that requires one and
  finds none is an init error naming `physical.atmosphere`, never a silent `rho = 0`).
- `if (physical.has("atmosphere"))`: `resolve_model_spec` → `load_environment_model` →
  `dynamic_cast<AtmosphereModel*>` with `PluginLoadError(spec.plugin_path, "does not implement
  AtmosphereModel")` on failure → add to `environment_scheduler_` and `environment_plugins_`.
  **No `check_position_representation` call** (Decision 3); that helper is untouched.
- Not added to `Vehicle::PhysicsSpec`: the EOM has no atmosphere input. PR 3's drag `WrenchSource` is
  the first consumer.
- The §15 pre-run check, beside lines 203/207:

```cpp
earth->validate_p_n(p_n, initial_state_node);
gravity->validate_p_n(p_n, initial_state_node);
if (atmosphere != nullptr) {
  // §15: "altitude inside the atmosphere model's valid range". EarthModel::altitude is the one
  // authority on turning p_n into a height (ADR-0003: p_n's meaning is the Earth model's).
  // Init-time only, like PR 1's validate_p_n: a trajectory that leaves the range mid-run is the
  // per-frame domain gap PR 1 disclosed, not this check's job.
  const double alt = earth->altitude(VehicleState{vehicle.state()});
  const AltitudeRange range = atmosphere->valid_range();
  if (!(alt >= range.min_m && alt <= range.max_m)) {   // NaN-safe: a NaN altitude fails this
    throw ConfigError(initial_state_node.origin(), <…".p_n" path…>,
                      "initial altitude is outside " + atmosphere_spec.plugin_path +
                          "'s valid range");
  }
}
```

  using the same `node.path().empty() ? "initial.state.p_n" : node.path() + ".p_n"` idiom all three
  PR 1 validators use. The message names the altitude, both bounds, and the atmosphere slot.

### New: the golden — **needs Decision 1, and the guard lifted**

Two files, both human-owned, produced by the procedure at the top of this plan:

- `tests/golden/atmosphere/generate_isa1976_table.py` — the independent generator. Document-verified
  constants at the top, the layer table with its altitude column labeled, the integration upward from
  `101325 Pa`, the base-pressure cross-check, and the header emitter. No ASTRA imports. Written
  ruff-clean from the start (`ruff check` + `ruff format --check`) — it lives under a guarded path, so
  a lint failure discovered later is a stop-and-ask for you (`docs/lessons.md` 2026-09-14, the
  `adr0001_cases.hpp` clang-tidy entry), not something this session can self-authorize fixing.
- `tests/golden/atmosphere/isa1976_table.hpp` — the house format: `#pragma once`, namespace
  `astra::test::isa1976`, `NOLINTBEGIN(modernize-avoid-c-arrays)` POD row structs, and **two separate
  arrays**:
  - `kTranscribedRows` — the eight table points (0/11/20/32/47/51/71/86 km) transcribed verbatim from
    the document's own tables, each `T`/`p`/`rho` a literal copied by eye, not computed;
  - `kComputedRows` — the dense rows the Python generator produces from the document-verified
    constants, for continuity/layer-boundary coverage.

  Tolerances for each array are stored beside it and differ in kind, not just size:
  - `kTranscribedRows`' tolerance is half a unit in the last printed place of each column — a
    printed-precision bound, independent of any implementation.
  - `kComputedRows`' tolerance is set from the two implementations' own agreement (double-precision
    evaluation of the same closed forms in Python and C++, sharing only the transcribed constants).

  Provenance block cites the document, edition, and table/page for both arrays separately — the shape
  of `tests/golden/nesc/case1_dragless_sphere.hpp`.

Guard: you are touching `.claude/touch-goldens`; it was absent when I checked (the `SessionStart`
hook deletes any marker touched before launch), so I re-check before writing these two files. And
per step 7, the guard is not really lifted until you have read the constants and base pressures
against the PDF. Nothing else in the PR is gated on any of this.

### New / changed: tests

- `tests/unit/atmosphere/isa1976_test.cpp` (new) — house pattern: load the real `.so` through
  `PluginLoader` at `ASTRA_TEST_PLUGIN_DIR`, `dynamic_cast`, `init()` with an inline
  `Config::from_string("{}\n", "isa1976-fixture")`, exercise as a pure function. Covers:
  - **`kTranscribedRows` matched to printed precision** (half a unit in the last printed place, per
    column) — this, not the dense array, is what "ISA table points exact" (§16, `PLAN.md`) names: the
    one check where the plugin is judged against the document, not against another implementation of
    the same formula. A wrong exponent or lapse sign shared by the plugin and the Python generator
    would still pass `kComputedRows`; only this array can catch it.
  - `kComputedRows`, `SCOPED_TRACE`d by altitude, at the generator's own tolerance;
  - continuity of `T`, `p`, `rho` across all seven layer boundaries;
  - sea level equals the defining `288.15 K / 101325 Pa / 1.225 kg·m⁻³`;
  - `rho` and `a` self-consistent with `p`, `T` via the ideal gas law and `sqrt(gamma·R·T)`;
  - `valid_range()` returns the expected pair;
  - **bounds are inclusive**: `at(min_m)` and `at(max_m)` are finite;
  - **clearly outside is NaN**: `at(-6000.0)` and `at(90000.0)`, not values a hair past the bound.
    `docs/lessons.md` 2026-09-15 (the `tan(π/2)` entry) is exactly this trap — never assert
    finite/non-finite behavior at a boundary approached through a double's rounding.
- `tests/unit/kernel/simulation_physical_config_test.cpp` — rewrite
  `RejectsAnUnknownPhysicalSlotName` to use a genuinely unknown slot name (it currently asserts
  `physical.atmosphere` is rejected, and its comment says why it goes away here); add: a `physical`
  block *with* atmosphere loads; a block *without* atmosphere still loads (the optionality guarantee
  every existing fixture relies on); an initial altitude above the ISA ceiling is a `ConfigError`
  naming `initial.state.p_n`.
- `tests/unit/CMakeLists.txt` — add `atmosphere/isa1976_test.cpp` to `astra_unit_tests` and
  `target_include_directories(astra_unit_tests PRIVATE "${ASTRA_SOURCE_DIR}/tests")` so the golden
  resolves as `"golden/atmosphere/isa1976_table.hpp"` (the `astra_frames_test` trick).
- No Python work: the bindings expose only `astra::Model`, and `tests/python/test_loader.py` already
  uses the `isa1976` path purely as a string fixture.
- `tests/unit/plugin/load_all_test.cpp` picks the new plugin up automatically via generated
  `kAllPlugins`.

### Changed: docs and process files

- `docs/architecture.md` §7 — define `AtmoState` in the code block and add `noexcept` to `at()`, so
  spec and code agree. (§7's `GravityModel` line is likewise looser than the real header; leaving it,
  since it is not this PR's interface.)
- `PLAN.md` — check PR 1b's box; correct "the HealthMonitor init path" to
  `Simulation::from_config`; strike the promised position-representation extension with its reason.
- `STATE.md` — rewritten by `/wrap` at the end.

## Out of scope (deliberately)

- PR 1's disclosed per-frame Earth/gravity domain-validity gap. Still a separate, plan-reviewed
  `HealthMonitor` change; not bolted on here.
- The stale comment in `gravity_model.hpp:37-39` claiming `wgs84_j2` does not override
  `validate_p_n` (a later Codex round added the override). One wrong line, adjacent but not mine —
  say the word and I will fix it here, otherwise it stays.

## Verification

1. NASA-TM-X-74335 fetched and read; constants, base pressures, and the eight transcribed table rows
   cross-checked and read by you against the PDF. `.claude/touch-goldens` present. This gates the two
   golden files only.
2. The gate, run once after the work, per `CLAUDE.md`: `just check` via the Bash tool's own
   `run_in_background` (never `nohup … &`), then block on the real process with
   `tail --pid=$(pgrep -x just) -f /dev/null` and read the log's `EXIT=0` / `check: OK` line. Nothing
   is verified before that line has been read. Expect 140 + N tests on each of `debug`, `release`,
   `asan`, `static-plugins`. Per `docs/lessons.md` 2026-09-15, no tracked file gets edited once the
   run is in flight.
3. Targeted, while iterating: `ctest --preset asan -R 'Isa1976|SimulationPhysicalConfig'`.
4. Review before the PR, from Bash in this session (physics diff, so both):
   - `node "$(find ~/.claude/plugins -name codex-companion.mjs | head -1)" review --base origin/main --wait`
   - the same script, `adversarial-review --base origin/main --wait`, focused on ADR-0002 (SI units,
     signal metadata), ADR-0003 (`p_n` meaning and the `EarthModel::altitude` handoff), ADR-0009
     (plugin layout), §14's no-throw/no-alloc per-frame rule for `at()`, and whether the C++ plugin
     and the Python generator agree by construction or only by coincidence (a shared wrong exponent
     or lapse sign is the one failure the two-implementation setup could still miss).
5. You read the physics diff (`plugins/physical/`, both golden files).
6. `/wrap` to rewrite `STATE.md`, push the branch, and open the PR.

## Acceptance

`just check` exits 0 with `check: OK` on every preset; **ISA table points exact** — the plugin matches
the eight transcribed table rows to printed precision — and the dense golden rows, computed
independently in Python from constants transcribed out of NASA-TM-X-74335 and cross-checked against
their own published base pressures, agree at the generator's own tolerance.
