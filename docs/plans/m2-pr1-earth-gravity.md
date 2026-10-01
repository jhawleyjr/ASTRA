# M2 PR 1 — AtmosphereModel + ISA-1976, geodetic Earth models, altitude-dependent gravity

Recovered from `~/.claude/plans/draft-m2-pr-1-snug-neumann.md` on 2026-09-30. Approval: the file records none; the work merged as #22, with the atmosphere moved to M2 PR 1b (#24). Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

M1 closed with a rigid body on a **flat** Earth. `EarthModel` already declares `p_n_dot`,
`omega_ie_n`, `omega_en_n`, `altitude`, and `core/src/kernel/eom.cpp` already consumes all four —
every Earth term is simply zero for flat. ADR-0003 says `p_n` means geodetic lat/lon (rad) +
altitude for round/WGS-84, and the EOM must not change when the Earth model does. Nothing has
exercised that claim yet.

This PR is `PLAN.md` M2 PR 1 as written there: `AtmosphereModel` + `isa1976`,
`earth/{round, wgs84_rotating}`, `gravity/{inverse_square, wgs84_j2}`, with initial `p_n` staying the
raw radians-and-metres vector range-checked by the Earth model at init. It also closes PR #21's open
adversarial-review finding at the gate (§5).

**Precondition on Joe:** `touch .claude/touch-goldens` at some point while this session runs — the
SessionStart hook deletes the marker, so it does not carry in from outside. Two files need it:
`tests/golden/atmosphere/isa1976_table.hpp` and `tests/golden/nesc/<wgs84_rotating_sphere>.hpp`,
both of whose numbers Joe generates independently (the `adr0001_cases.hpp` precedent).

## 1. Position access: no new API, plus a representation compatibility check

Settled by Joe, not open: **ADR-0003 already makes `p_n` geodetic** (latitude, longitude in rad;
altitude in m) under `earth/round` and `earth/wgs84_rotating`. So there is nothing to invent.

- `GravityModel::g_n(vs)` reads latitude and altitude **straight from `vs.p_n()`** —
  `latitude = p_n(0)`, `altitude = p_n(2)`. Reading a layout ADR-0003 defines is not reinterpreting it.
- `AtmosphereModel::at(altitude)` takes its altitude from the **existing `EarthModel::altitude(vs)`**.
- **No `Geodetic` struct, no `geodetic()` accessor, no `bind_earth` hook.** `EarthModel::altitude()`
  and `GravityModel::g_n()` keep their M1 signatures; `plugins/physical/earth/flat/flat.cpp` is
  untouched.

What remains is the one real hazard: a gravity or atmosphere model that assumes geodetic `p_n`,
paired in config with `earth/flat`, would read NED metres as radians and produce plausible garbage.
So **each model declares the position representation it requires, and `Simulation::from_config`
rejects a mismatch at init**:

```cpp
// core/include/astra/interfaces/position_representation.hpp (new, tiny)
enum class PositionRepresentation { kAny, kFlatNed, kGeodetic };
```

- `EarthModel` — new pure virtual `position_representation()`, because every Earth model must answer
  what it *provides*: `flat` → `kFlatNed`; `round` and `wgs84_rotating` → `kGeodetic`.
- `GravityModel` and `AtmosphereModel` — each gets its own non-pure
  `assumed_position_representation()` defaulting to `kAny`, defined out of line. `inverse_square` and
  `wgs84_j2` override it to `kGeodetic`. `gravity/constant` and `atmosphere/isa1976` leave the
  default: neither reads `p_n` (isa1976 gets its altitude through `EarthModel::altitude`, which every
  Earth model answers correctly in its own representation).
  The declaration is duplicated across the two interfaces rather than hoisted onto `Model`
  deliberately — PLAN.md scopes it to "each gravity and atmosphere model", and `Model` is the most
  pinned interface in the tree. Two identical five-line declarations is the cheaper price.
- `Simulation::from_config` compares each loaded model's assumption against the Earth model's
  provided representation and throws `ConfigError` naming **`physical.gravity`** (or
  `physical.atmosphere`) **and `physical.earth`**, so the message says which two keys disagree. In
  practice this is what rejects `earth/flat` paired with `gravity/wgs84_j2`.

## 2. `AtmosphereModel` / `AtmoState`

`docs/architecture.md:196` sketches `virtual AtmoState at(double altitude_m) const = 0`; `AtmoState`
is defined nowhere. New `core/include/astra/interfaces/atmosphere_model.hpp`:

```cpp
struct AtmoState {
  double density = 0.0;         // kg/m^3
  double pressure = 0.0;        // Pa
  double temperature = 0.0;     // K
  double speed_of_sound = 0.0;  // m/s
};
struct AltitudeRange { double min = 0.0; double max = 0.0; };  // geometric, inclusive

class AtmosphereModel : public Model {
 public:
  ~AtmosphereModel() override;                      // out-of-line key function, see §4
  [[nodiscard]] virtual AtmoState at(double altitude) const noexcept = 0;
  [[nodiscard]] virtual AltitudeRange valid_range() const noexcept = 0;
};
```

Two deliberate deviations from §7's sketch, both with M1 precedent:

- **`noexcept`** — M1 strengthened §7's sketches the same way (`mass_properties_model.hpp:19-21`:
  the per-frame path is `noexcept` end to end).
- **`altitude`, not `altitude_m`** — ADR-0002 forbids unit suffixes in identifiers and
  `EarthModel::altitude()` already follows the ADR; §7's sketch predates it. *Your note wrote
  `at(altitude_m)`, §7's spelling — say the word if you want the suffix kept and I'll use it.*

`at()` takes **geometric** altitude and converts to geopotential internally via `H = r0*h/(r0+h)`,
`r0 = 6356766 m`. It is **total**: outside `valid_range()` it clamps to the nearest endpoint, never
throwing (it is `noexcept`; throwing would terminate) and never returning NaN.

**§15 pre-run altitude-range check joins the `HealthMonitor` init path**, per PLAN.md, rather than
living as a bespoke check in `Simulation::from_config`. `HealthMonitor::init` already reads the
optional `sim.health` block, may throw `ConfigError`, and runs before `registry.freeze()` — the exact
init-time slot `docs/architecture.md:345`'s "altitude inside the atmosphere model's valid range"
needs. New optional parameters, both null when there is no atmosphere or no physics:

```cpp
// Same call site and contract as today (may throw ConfigError; before registry.freeze()). atmosphere
// and initial_altitude are both non-null only when physical.atmosphere is configured; either null
// skips the check (docs/architecture.md §15: the range check only applies once there is a range).
void init(const Config& config, SignalRegistry& registry,
          const AtmosphereModel* atmosphere = nullptr,
          std::optional<double> initial_altitude = std::nullopt);
```

(`std::optional<double>` over a `const double*` per your note — it reads better and carries "absent"
in the type rather than in a comment. `init` is init-path, so the extra bytes cost nothing.)

`Simulation::from_config` computes `earth->altitude(initial_state)` into a local, passes its address
along with the loaded `AtmosphereModel*`, and `HealthMonitor::init` does the range comparison and
throws naming `initial.state.p_n` when it fails. This is the enforcement point; §14 keeps step-time
model errors on status flags, and a per-frame `health.atmosphere_range` check belongs with the first
per-frame consumer (M3 aero), not here — `HealthMonitor`'s existing per-frame checks
(`check_wrench`/`check_state`) are untouched by this PR.

## 3. Earth models and `validate_p_n`

`plugins/physical/earth/round/` — sphere, non-rotating, `radius` from config (default 6371000 m).
With `v_n = C_bn * v_b = [v_N, v_E, v_D]`:

- `p_n_dot = [v_N/(R+h), v_E/((R+h)cos(lat)), -v_D]`
- `omega_ie_n = 0`
- `omega_en_n = [v_E/(R+h), -v_N/(R+h), -v_E*tan(lat)/(R+h)]`

`plugins/physical/earth/wgs84_rotating/` — ellipsoid, rotating; same shape with `M_r` (meridian) and
`N_r` (prime vertical) from `geodesy.hpp` in place of `R`, and
`omega_ie_n = omega_ie * [cos(lat), 0, -sin(lat)]`.

**`validate_p_n`** — new init-path hook, default no-op (flat Earth accepts any finite NED triple):

```cpp
virtual void validate_p_n(const Eigen::Vector3d& p_n, const Config& node) const;  // may throw ConfigError
```

Round and WGS-84 override it: longitude in `[-pi, pi]`, and latitude bounded **away from** the poles
rather than merely at them. `lon_dot = v_E/((N+h)cos(lat))` and the `tan(lat)` term in `omega_en_n`
are singular at `|lat| = pi/2`, so admitting the poles hands the integrator a NaN on frame 0. Latitude
must satisfy `|lat| <= kMaxLatitude`, with `kMaxLatitude = pi/2 - 1e-6` rad (≈ 6 m from the pole)
stated once in `geodesy.hpp`.

**Altitude is not range-checked against the surface here.** A below-surface altitude is not
inherently invalid — the Dead Sea shore is at −430 m, and `isa1976`'s own valid range starts at
−5000 m — so a floor at `h = 0` would reject legitimate low-altitude starts. `validate_p_n` only
requires altitude to be **finite** and to satisfy `h > -R` (`-N_r` for WGS-84 at the given latitude):
below that the position-kinematics denominators `(R+h)` / `(N_r+h)` themselves go through zero, which
is a chart singularity, not a terrain check. The atmosphere model's `valid_range()` (§2's
`HealthMonitor` check) is the actual authority on whether an altitude makes physical sense for a run;
`validate_p_n` only protects the kinematics from dividing by zero.

Messages: `|lat| > pi/2` reports **"latitude is radians, not degrees (ADR-0003)"** — a degree-valued
`p_n` is the obvious failure mode; between `kMaxLatitude` and `pi/2` it reports the polar-singularity
message instead. In-run pole crossing is a documented chart limitation, not new machinery: the
existing `HealthMonitor` already halts on NaN in state naming `v0.state.p_n`, so it degrades to a
named halt rather than silent garbage.

`Simulation::from_config` calls `validate_p_n` once after `read_initial_state`, which gains a one-line
comment saying `p_n`'s units depend on the configured Earth model, pointing at ADR-0003, so the next
session does not re-ask.

## 4. Gravity, atmosphere, geodesy

- `plugins/physical/gravity/inverse_square/` — `g_n = [0, 0, g0*(R/(R+h))^2]`, `h = vs.p_n()(2)`.
  Config `g0` (default 9.80665) and `radius` (default 6371000). The radius duplicates
  `earth/round`'s; a known wart, kept because the alternative is a `radius` concept on `EarthModel`
  that means nothing for flat Earth. The test config sets both from one comment.
- `plugins/physical/gravity/wgs84_j2/` — gravitation from the J2-truncated potential plus the
  centripetal term, in NED, from `vs.p_n()`'s latitude and altitude. Plumb-bob by construction, which
  is what `gravity_model.hpp:2-3` requires of `g_n`.
- `plugins/physical/atmosphere/isa1976/` — seven layers from the standard's defining constants: base
  geopotential altitudes `[0, 11000, 20000, 32000, 47000, 51000, 71000]` m, base temperatures
  `[288.15, 216.65, 216.65, 228.65, 270.65, 270.65, 214.65]` K, lapse rates
  `[-6.5, 0, +1.0, +2.8, 0, -2.8, -2.0]` K/km, `P0 = 101325` Pa, `g0 = 9.80665`,
  **`R* = 8.31432` J/(mol·K)** (the 1976 value, *not* CODATA — the modern constant silently breaks
  every table point), `M = 0.0289644` kg/mol, `gamma = 1.4`. Valid geometric range `[-5000, 86000]` m.
  Reads no config in this PR.
- `core/include/astra/util/geodesy.hpp` (new) — header-only `astra::geodesy`, mirroring `frames.hpp`:
  WGS-84 constants (`a = 6378137.0`, `f = 1/298.257223563`, `e2`, `omega_ie = 7.292115e-5`,
  `GM = 3.986004418e14`, `J2 = 1.082629989e-3`), `meridian_radius(lat)`, `prime_vertical_radius(lat)`,
  `kMaxLatitude`. The `core/util/` "geodesy" slot `docs/architecture.md:67` reserves.

**Core plumbing:** `core/src/interfaces/atmosphere_model.cpp` (new) defines `~AtmosphereModel()` —
`earth_model.hpp:18-23` documents why a key function is mandatory: under `-fvisibility=hidden` a
plugin TU without one emits its own vtable/typeinfo and breaks `dynamic_cast` and UBSan's vptr check
across the `.so` boundary. The new non-pure virtuals — `validate_p_n` and `position_representation`
on `EarthModel`, `assumed_position_representation` on `GravityModel` and `AtmosphereModel` — are
likewise defined out of line in each interface's own existing/new `.cpp`, so the destructor stays each
class's key function. `core/src/kernel/simulation.cpp` gains the optional `physical.atmosphere` slot
through the existing `resolve_model_spec` + `load_environment_model` (unprefixed signals; ADR-0011:
environment is shared), the representation check, `validate_p_n`, and passing the atmosphere pointer
and initial altitude to `HealthMonitor::init` (§2 above). Atmosphere stays **optional** so every
existing config remains valid. `core/CMakeLists.txt` picks up the new source.

## 5. Closing PR #21's adversarial-review finding at the gate

That finding was that `tests/support/alloc_counter` replaces global `operator new`/`delete` but misses
a direct `malloc`/`calloc` call from a plugin. PR #21 documented the limit rather than overriding
`malloc` under ASan (real conflict risk). The static-analysis route closes it with no runtime risk:

- `.clang-tidy` — add `cppcoreguidelines-no-malloc` to `Checks:` (that one check, not the family),
  with a comment giving the reason, **plus a `CheckOptions` entry widening what it matches**:

  ```yaml
  cppcoreguidelines-no-malloc.Allocations: '::malloc;::calloc;::aligned_alloc;::posix_memalign'
  ```

  Without this the check's default `Allocations` list is only `::malloc;::calloc`, so
  `aligned_alloc` — which `alloc_counter` itself uses — and `posix_memalign` would both sail
  straight through, and the finding would be only half closed. `Deallocations` (`::free`) and
  `Reallocations` (`::realloc`) stay at their defaults, which already cover the rest.
  `WarningsAsErrors: '*'` then makes any direct C-allocator call under `core/`, `plugins/`,
  `aircraft/`, `tests/` a `just check` failure instead of something that slips past the counter.
- `tests/support/alloc_counter.cpp` — one `NOLINTBEGIN(cppcoreguidelines-no-malloc)` /
  `NOLINTEND(cppcoreguidelines-no-malloc)` pair around the whole global replacement set (the aligned
  helper through the last `operator delete`, roughly lines 15–114), whose forwarding to
  `std::malloc` / `std::free` is the one legitimate use in the tree. A single pair, not per-line
  suppressions.

Verified there is nothing else to suppress: the only other `malloc`/`free` occurrences in the tree are
in comments (`alloc_counter.hpp`).

## 6. Tests, and every CMake edit they need

- `tests/golden/atmosphere/isa1976_table.hpp` **(marker)** — `adr0001_cases.hpp` format, with a
  `struct Case` carrying **both height bases** (`geopotential_altitude`, `geometric_altitude`) and
  **two tolerances**: `1e-12` against values recomputed at full precision from the standard's defining
  equations, and a per-field printed-precision tolerance against the published rounded table values.
  Both are needed for "exact" to mean anything: published tables are rounded, and the 11 km
  *geopotential* layer base is 11019.0678 m *geometric*, so a test passing 11000 to a geometric-input
  `at()` would silently query the wrong point.
- `tests/golden/nesc/<wgs84_rotating_sphere>.hpp` **(marker)** — the NESC dropped-sphere case on the
  WGS-84 rotating Earth (NASA/TM-2015-218675): initial condition, reference states at a small set of
  checkpoint times, and the per-channel tolerances the data provides. PR 1 does **not** build the case
  harness — PR 3 does, from YAML under `tests/physics/nesc/` — so this lands as one golden header plus
  one hand-written test that PR 3 absorbs.
  **First step, before any golden is written: read TM-2015-218675 and confirm which gravity the chosen
  drag-free case actually specifies** — spherical inverse-square, WGS-84 ellipsoid without J2, or
  WGS-84 with J2 — and pair the gravity plugin to the case rather than assuming `wgs84_j2`. If the
  case specifies a gravity this PR does not build, that decides which case to use (or adds the missing
  one), and it decides it before the oracle exists rather than after the test fails.
- `tests/unit/atmosphere/isa1976_test.cpp` → **new `astra_atmosphere_test` executable** added to the
  explicit source list in `tests/unit/CMakeLists.txt`, `TEST_PREFIX "atmosphere."`,
  `target_include_directories(... ${ASTRA_SOURCE_DIR}/tests)` for the golden — the `astra_frames_test`
  pattern. Loads the real `.so` through `PluginLoader` + `dynamic_cast<AtmosphereModel*>`, so it tests
  the shipped artifact and works under `static-plugins`. **Must be added to the explicit `ITEMS` list
  in `astra_finalize_plugins()` (`cmake/AstraPlugin.cmake:155`)** or it races the plugin build.
  Cases: every golden point called with its **geometric** altitude, to both tolerances; continuity
  across all seven layer boundaries; the geometric↔geopotential conversion against a hand-computed
  pair; clamping (not NaN, not throw) beyond both endpoints.
- `tests/unit/earth/earth_kinematics_test.cpp` → `astra_unit_tests`. The chart tested as **pure
  functions** — `p_n_dot`, `omega_en_n`, `omega_ie_n`, `altitude` on hand-built `VehicleState`s against
  closed forms typed out in the test. Integrating a trajectory to check these would need a lift force
  no plugin supplies and would compare the code to itself. Cases: due-north and due-east motion at
  several latitudes; the spherical limit (`M_r = N_r = R`); `omega_ie_n` at equator and mid-latitude;
  `validate_p_n` rejecting a degree-valued latitude with the ADR-0003 message and `|lat| >
  kMaxLatitude` with the polar message.
- `tests/unit/util/geodesy_test.cpp` → `astra_unit_tests`. Radii of curvature against closed forms at
  equator/45°/pole; the flattening and eccentricity identities.
- `tests/unit/gravity/wgs84_j2_test.cpp` → `astra_unit_tests`. J2 against the **Somigliana** closed
  form on the ellipsoid surface across latitude — two independent formulations, so the test judges the
  implementation rather than itself, with no external data. Within 1e-4 m/s².
- `tests/unit/kernel/simulation_physical_config_test.cpp` — new cases: the optional atmosphere slot;
  **`earth/flat` + `gravity/wgs84_j2` rejected at init**, the message naming `physical.gravity` and
  `physical.earth`; `earth/round` + `gravity/constant` still accepted (`kAny` is compatible with
  everything). **`RejectsAnUnknownPhysicalSlotName` (lines 93-110) currently uses `atmosphere:` as its
  unknown slot and must switch to a name that stays unknown** — that retargets a fixture; the
  assertion and its strictness are unchanged.
- `tests/unit/health/health_monitor_test.cpp` — the altitude-range check in its new home: accepted
  inside the range, `ConfigError` naming `initial.state.p_n` outside it at each end, and skipped
  entirely when the atmosphere pointer is null. The existing per-frame cases are untouched.
- `tests/physics/round_earth_drop_test.cpp` and `tests/physics/nesc_sphere_test.cpp` → added to the
  explicit source list in `tests/physics/CMakeLists.txt`, with configs in `tests/configs/`:
  - **Round non-rotating drop vs the inverse-square closed form.** `earth/round` +
    `gravity/inverse_square`, released from rest at altitude `h0`. Two independent exact checks, both
    forward evaluations of the closed form — no inversion, no reliance on the integrator: the energy
    integral `v^2 = 2*GM*(1/r - 1/r0)` at the final state, and the radial-fall time
    `t(r) = sqrt(r0^3/(2*GM)) * (acos(sqrt(r/r0)) + sqrt((r/r0)*(1 - r/r0)))` evaluated at the final
    radius and compared against `frames * dt`. Plus latitude and longitude unchanged — a purely radial
    fall, which is also what keeps this case clear of the polar singularity.
  - **NESC WGS-84 rotating sphere drop** against the golden, within its stated tolerances.
    `earth/wgs84_rotating` paired with whichever gravity plugin TM-2015-218675's case actually
    specifies (see above), no drag. **`omega_b` is relative to *inertial* (ADR-0003), so a sphere at
    rest relative to the rotating Earth starts with `omega_b = C_nb * omega_ie_n`, not zero** — with
    level attitude and heading 0 that is `omega_ie * [cos(lat), 0, -sin(lat)]`. Zero would feed the
    EOM's `-(omega_b + omega_ie_b) x v_b` one Earth rate where two are physical, and the case would
    "fail" against a correct EOM. `v_b = 0` *is* right, because `v_b` is already defined relative to
    the Earth-fixed frame. The NESC case's own initial-condition conventions get converted to
    ADR-0001/0003 form once, in the config, with the conversion written out in a comment.
    **One cheap independent invariant added alongside the golden comparison**: with that `omega_b`
    initial condition, `q_nb` must stay level (roll and pitch within the drop's own transport-rate
    bound, which is tiny for a short vertical drop) over the whole run. A wrong sign on `omega_ie_b`
    anywhere in the chain shows up here — as visible tumbling — before it shows up as a subtler
    trajectory-tolerance failure, and it costs nothing beyond one more assertion on data already
    being generated.
- `PLAN.md` — tick M2 PR 1.

## 7. Verification

Gate, run once after the work, via the Bash tool's own `run_in_background` (never `nohup … &`), then
block on the real process and read the log's `EXIT=0` / `check: OK` line:

```
just check
```

Acceptance selector for this PR — `--no-tests=error` so a selector matching nothing fails rather than
silently passing (the `justfile`'s own convention):

```
ctest --preset asan -R 'atmosphere|geodesy|earth_kinematics|wgs84|round_earth|nesc' \
      --no-tests=error --output-on-failure
```

Bars: `just check` exits 0 on every preset; ISA golden points to 1e-12 against the recomputed values
and to printed precision against the published ones; the round non-rotating drop matches the
inverse-square closed form to 1e-9 relative on both the energy integral and the fall time; the NESC
WGS-84 rotating sphere drop within the tolerances its data provides, and its attitude staying level
over the drop; J2 vs Somigliana within 1e-4 m/s²; Earth kinematics to 1e-12 against the closed forms.

Then, before the PR:

```
node "$(find ~/.claude/plugins -name codex-companion.mjs | head -1)" review --base origin/main --wait
node "$(find ~/.claude/plugins -name codex-companion.mjs | head -1)" adversarial-review --base origin/main --wait \
  "<focus: ADR-0001 NED/FRD signs in omega_en_n and omega_ie_n; ADR-0002 SI and no unit suffixes;
   ADR-0003 geodetic p_n, omega_b relative to inertial, and the EOM unchanged; the polar singularity;
   the position-representation check; no per-frame allocation or exceptions in at()/g_n()/p_n_dot>"
```

Physics-touching throughout (`core/util/`, `plugins/physical/`), so Joe reads the diff.

## 8. Plan review — the one round, and what came of it

Four findings folded in above:

1. **Polar singularity** — sharpest. `lon_dot` and the `tan(lat)` term are singular at `|lat| = pi/2`,
   and the due-north circumnavigation test in that draft necessarily crossed both poles: it would have
   shipped a NaN generator with a test that invoked it. Fix: `kMaxLatitude` rejection at init, the
   documented HealthMonitor backstop, and the circumnavigation replaced by pure-function kinematics
   checks.
2. **`omega_b` is inertial-relative**, so a body at rest on the rotating Earth starts with
   `omega_b = C_nb * omega_ie_n`; zero gives one Earth rate instead of two in the Coriolis term. This
   now governs the NESC sphere case's initial condition.
3. **"ISA table points exact" was not achievable as stated** — published values are rounded and the
   geopotential/geometric distinction shifts the query point. Fix: both height bases and both
   tolerances in the golden.
4. **`at()` is `noexcept` and cannot reject an out-of-range altitude** — the draft conflated the
   step-time and init-time paths. Fix: `at()` clamps and is total; rejection is the init-time
   `ConfigError`; endpoints inclusive.

One finding accepted in substance, **remedy rejected**: Codex proposed a test-only wrench source so an
integrated level-flight trajectory could hold altitude. The observation was right — no plugin supplies
lift, so that test was unrealizable — but a new test plugin is the wrong fix: the chart is a pure
function, and testing `p_n_dot` directly is simpler *and* more independent than integrating it. Its
other half, that comparing a round-Earth drop to the M1 flat-Earth result compares two simulator paths
rather than a closed form, is accepted; that test is gone, replaced by the inverse-square closed form
the acceptance names.

The §1 position-access design that round was going to be re-run on is settled by ADR-0003 instead
(Joe): no new API, plus the init-time representation check. One round per plan, and it has been had.

## 9. Risks

- **Size.** Four plugins, small interface additions on three headers, geodesy, nine test files, plus
  the clang-tidy item and the `HealthMonitor` signature change — at or just above M1 PR 3's ~1400
  lines. PLAN.md's own contingency: **if this exceeds ~1200 lines of new code, ISA-1976 splits into
  its own PR; gravity stays with the Earth models** (they share the position-representation
  machinery and the NESC acceptance case; ISA does not). Checked once the diff exists — this plan
  proceeds as one PR and splits only if the line count says so.
- PR 1 lands one NESC case as a golden header that PR 3's YAML harness will absorb. Deliberate
  duplication: building the harness here would pull PR 3 forward. PR 1 also cannot supply drag — the
  sphere/brick `WrenchSource` is PR 3's — so the case chosen here must be a drag-free one.
- PR 3 may want a **rotating spherical** Earth (`rotating: true` on `round`, per PLAN.md). Nothing in
  this PR precludes it: `round` keeps its Earth-rate terms as ordinary expressions that a later
  config flag can switch on, rather than hardcoding the zeros the non-rotating case produces.
- `kMaxLatitude = pi/2 - 1e-6` is a judgment call, not a derived number, stated in one place so a later
  nonsingular chart supersedes it cleanly.
- `HealthMonitor::init` gains two new optional parameters. Both default to "absent" and the one
  existing caller, `Simulation::from_config`, is updated in the same PR, so nothing breaks silently —
  but it is a signature change to the class M1 PR 4 built last session, worth your eye.
