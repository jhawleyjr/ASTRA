# M2 PR 2c — per-frame domain validity (ADR-0012), plus a separate docs commit

Recovered from `~/.claude/plans/m2-pr-2c-plan-md-keen-bubble.md` on 2026-09-30. Approval: the file records none; the work merged as #27. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

`EarthModel::validate_p_n`, `GravityModel::validate_p_n`, and the `AtmosphereModel::valid_range()`
comparison in `Simulation::from_config` all run **once, at init**. The properties they check are not
properties of the input — they are what the position kinematics and `at()` need at *every*
evaluation. `round`'s `p_n_dot` divides by `cos(lat)` and `(radius_ + h)`; `wgs84_rotating`'s by
`(M(lat) + h)` and `(N(lat) + h)`; `omega_en_n` carries `tan(lat)`. A trajectory that *approaches*
a boundary without landing on it runs on large-but-finite degraded kinematics and nothing observes
it — `HealthMonitor` checks only NaN/Inf and quaternion norm, which fire on the eventual overflow,
not on the approach. This is PR 1's polar-approach gap and PR 1b's mid-run atmosphere-range gap,
both disclosed in `PLAN.md` and `STATE.md` and both scheduled here.

ADR-0012 (accepted, on main) settles the design and is not reopened: a `HealthMonitor` check over a
**model-owned predicate**, checked **post-step with a one-frame lookahead**, not per-RK4-stage; and
atmosphere gains no new interface. Two things follow from §14: the run-time form is an *observation*,
never a throw; and the init and run predicates are **not the same** — longitude range and the
degrees-vs-radians check are input sanity and must stay init-only, or every antimeridian crossing
would halt.

Must land before PR 3, whose drag `WrenchSource` is the first consumer that can read a NaN
`AtmoState` into the EOM.

This session also lands a **separate, docs-only commit** (ADR-0013, unrelated to PR 2c) that must be
on main before PR 3. The oracle guard is armed (`/home/joe/repos/ASTRA/.claude/touch-goldens`).

---

# Part A — docs commit (first, on its own)

Docs only, no code. Committed before any PR 2c work so the two never interleave.

### A1. `docs/decisions/0013-nesc-check-case-oracle.md` (new)

Created verbatim from the content Joe supplied. Accepted, not proposed — not re-decided, not
reworded.

### A2. `AGENTS.md` becomes a symlink to `CLAUDE.md` again

PR #7 (`d47e347`) made `AGENTS.md` a symlink; PR #11 (`ef30a2f`) inverted it. Restoring the original
design in this commit:

- Move `AGENTS.md`'s 77 lines into `CLAUDE.md` **above** the `## Claude Code` section, wording
  unchanged.
- Delete `CLAUDE.md`'s `@AGENTS.md` line — with the symlink restored it would import itself.
- `git rm AGENTS.md && ln -s CLAUDE.md AGENTS.md && git add AGENTS.md`; verify
  `git ls-files -s AGENTS.md` shows mode `120000`.
- Mark the `## Claude Code` section as Claude-Code-only, since Codex now reads the same file.
- Add the `docs/refs/` "Read first" line: human-placed reference documents, gitignored, cited by
  page, pointing at `docs/refs/README.md`.
- Line arithmetic: 77 + 23 − 1 (the import) = 99, + 1 (`docs/refs/`) = **100**. No reflowing, no cap
  change.

Two wording consequences I will handle and flag rather than leave silently wrong:

- `AGENTS.md`'s intro sentence currently says Claude Code "reads it through `CLAUDE.md`
  (`@AGENTS.md` import) … Tool-specific process lives in `CLAUDE.md`, not here." With the symlink
  that is false and self-referential. I will rewrite those **two lines in place** (same line count)
  to describe the symlink. This is the one exception to "wording unchanged", and it is forced by the
  change itself.
- The `# AGENTS.md` H1 stays as-is (Codex reads the file under that name). Flagged, not changed.

### A3. `docs/process/workflow.md`

- §"Files that carry state": the `AGENTS.md` + `CLAUDE.md` row becomes **one file** capped at 100
  lines, symlinked for Codex.
- `.claude/skills/retro/SKILL.md` step 7 says "Check AGENTS.md + CLAUDE.md stay at or under 100 lines
  together" — with a symlink that double-counts. One-line correction, included here because this
  change is what breaks it. **Flagged for veto**: it is the only file outside Joe's a/b/c list.

### A4. `PLAN.md` M2 PR 3 line

Append `Oracle pipeline settled by ADR-0013.` and fold in the four facts the NESC data itself
settles, so PR 3 no longer decides them:

1. case 5 requires `rotating: true` on `round` — Table 6.3-1 pairs case 4 fixed / case 5 rotating
   with 1/R², to isolate Earth rotation;
2. case 8's wind column is f(h), so the varying-wind model is an altitude-keyed
   `plugins/physical/wind/table`, not a time-varying one (this resolves the choice PR 3's line
   currently leaves open);
3. "cases 1–10" is exactly Table 6.3-1's sphere-and-brick set; the shipped data has 17 atmospheric
   cases, 11+ being F-16 and two-stage-rocket, out of scope until M3;
4. PR 1b's geoid gap is now load-bearing, not hypothetical: the reference column is `altitudeMsl_ft`
   (MSL) while `wgs84_rotating::altitude()` returns height above the ellipsoid — it affects all eight
   WGS-84 cases.

---

# Part B — PR 2c

## Decisions on the four points ADR-0012 leaves open

**1. Earth and gravity reach the check through new `Eom` accessors, not retained `Vehicle` pointers.**
`Eom` already holds `EarthModel&`/`GravityModel&` as reference members (non-null by construction) and
already computes `p_n_dot`; `PhysicsSpec`'s pointers are consumed at init and not retained. Adding
`const EarthModel& earth() const noexcept` / `const GravityModel& gravity() const noexcept` to `Eom`
leaves `Eom::operator() const noexcept` completely untouched (ADR-0004: one derivative functor), and
avoids a second source of truth for "which Earth model this vehicle integrates against". The check is
then naturally gated on `eom_.has_value()`, which is exactly where it belongs.

**2. The atmosphere check runs in `Simulation::step`, on the POST-step state, and duplicates nothing.**
`Simulation::step` already calls `earth_->altitude(vs)` — but at **frame-start** state, **before**
`vehicle.step()`/`integrate()`, and **only when `wind_ != nullptr`**. The domain check needs the
**post-step** altitude. These are two different states, so there is no call to share and nothing to
hoist; the new call is made once per vehicle per frame, immediately after `vehicle.integrate(...)`
returns a falsey halt, from a new non-owning `AtmosphereModel* atmosphere_` member (plus
`std::string atmosphere_plugin_path_` for `HaltReason::source`'s lifetime). Post-renormalization
placement is equivalent to ADR-0012's "before `post_step_renormalize`": renormalization touches only
`q_nb`, never `p_n`, so `altitude()` returns the same double either way.

This also gives PR 3 exactly the guarantee it needs: PR 3 latches `EnvironmentSample::atmosphere` at
frame start, and frame N's post-step state *is* frame N+1's frame-start state. Frame 0's is validated
by `from_config`'s existing init check. So every state a drag `WrenchSource` will ever see has been
range-checked before it is sampled.

No lookahead for atmosphere: ADR-0012 pins one only for the `p_n` singularities, and an
out-of-range altitude is not a singularity — `at()` returns all-NaN, which `check_wrench` catches
once a consumer exists.

**3. `health.domain` is a fire count**, `double domain_count_` registered as `"health.domain"`, unit
`"1"` — identical in shape to `health.nan` and `health.quaternion`, counted before the halt decision
by the existing `apply()` helper so a halting event still lands in the log. (`health.max_q_drift` is
a max, and has no analogue here: "how close to the boundary" is not a single scalar across
latitude/altitude/atmosphere.)

**4. `in_domain`'s default-true base implementation lives in the base class**, defined **out of line**
in `core/src/interfaces/earth_model.cpp` and `gravity_model.cpp` — `flat` and `gravity/constant`
override nothing. This is exactly `validate_p_n`'s existing shape and exists for the same reason: the
out-of-line destructor must stay the key function, or every `-fvisibility=hidden` plugin TU emits its
own vtable and typeinfo and breaks `dynamic_cast` across the `.so` boundary. Out-of-line costs
nothing here — it is a virtual call that could not be devirtualized across a `.so` regardless. The
"a future model forgets to override and silently passes" risk is mitigated by decision 5: a model
with a singularity necessarily writes a `validate_p_n`, and that rewrite is what forces it to express
the singular part as `in_domain`. **Exactly two models keep the base `true`: `earth/flat` and
`gravity/constant`.**

**5. Per-plugin, one definition of "singular" via a file-local fault enum.** ADR-0012 requires
`validate_p_n` = `in_domain` + input sanity, but `validate_p_n`'s distinct, useful messages must
survive. Each plugin gets a file-local

```cpp
enum class DomainFault : std::uint8_t { kNone, kNonFinite, kPolarSingularity, kBelowSurfaceCenter };
[[nodiscard]] DomainFault domain_fault(const Eigen::Vector3d& p_n, double radius) noexcept;
```

`in_domain` is `domain_fault(...) == kNone`; `validate_p_n` switches on the same helper for the
finite / polar / floor messages and interleaves its two init-only checks in the current order, so
every existing message and ordering is preserved byte for byte:

1. `kNonFinite` → "p_n must be finite"
2. **init-only input sanity**: `|lat| > pi/2` → "latitude is radians, not degrees (ADR-0003)"
   (checked before the polar message so the more helpful one wins)
3. `kPolarSingularity` → the polar-margin message
4. **init-only input sanity**: `lon` outside `[-pi, pi]` → the longitude message. Commented with
   ADR-0012's reason: nothing in the tree wraps longitude and nothing is singular in it, so a
   mid-run antimeridian crossing must **not** be a domain fault.
5. `kBelowSurfaceCenter` → the altitude-floor message

`in_domain` is **NaN-safe by construction** (positive-form comparisons: `isfinite && |lat| <=
kMaxLatitude && alt > -R`). This matters: with `sim.health: { nan: warn }`, `check_state` returns
falsey on a NaN state and the domain check still runs.

**5b. `gravity/wgs84_j2` gets `in_domain` too.** I had it wrong; Joe caught it. It does **not** have a
no-op `validate_p_n` — `wgs84_j2.cpp:64-91` guards `kMinSafeRadius = 1000.0` m against ECEF-radius
collapse, from a Codex adversarial-review finding recorded in `docs/lessons.md` 2026-09-15: near the
polar margin, `lat = kMaxLatitude` with `alt = -N(lat)*(1-e^2)` passes `wgs84_rotating`'s own `M(lat)`
floor while `mag_r` collapses to ~0.043 m. Leaving it on the base `true` would ship per-frame domain
validity that misses the one gravity singularity this repo has already been bitten by — ADR-0012's
exact target shape. It is the simplest of the three, because it has no input-sanity part:

```cpp
bool in_domain(p_n) const noexcept override {
  return geodesy::geodetic_to_ecef(p_n(0), p_n(1), p_n(2)).norm() >= kMinSafeRadius;
}
void validate_p_n(p_n, node) const override { if (!in_domain(p_n)) throw <existing message>; }
```

The `>=` positive form is NaN-safe where the existing `mag_r < kMinSafeRadius` was not, so a NaN `p_n`
now throws at init instead of passing — strictly better, and unreachable in practice because the
Earth model's `validate_p_n` runs first and rejects non-finite `p_n`.

Cost: one `geodetic_to_ecef` per evaluation, twice per frame with the lookahead. So
`tests/configs/no_alloc_domain.yaml` uses `wgs84_rotating` + `wgs84_j2` (the expensive pairing), and
the gravity-floor halt test keeps `inverse_square`.

`gravity_model.hpp`'s `validate_p_n` comment currently says "Default no-op (gravity/constant **and
gravity/wgs84_j2**…)" — stale, and what I misread. Corrected in the same change.

**5c. Frame 0's RK4 stages have no lookahead behind them**, and that is not left unnoticed. Decision
2's coverage argument (frame N's post-step state is frame N+1's frame-start state) holds from frame 1
on; frame 0's stages run before any domain check exists, and init's `validate_p_n` validates `p_n(0)`
itself, not the point a frame-0 stage can reach. I close it rather than document it: `Vehicle::
integrate` runs the **same** `check_domain` helper on the **pre-step** state when `ctx.frame == 0`,
before the wrench loop, returning a halt with `state_` untouched. One extra `p_n_dot` on frame 0 only,
one well-predicted branch thereafter, and it makes the invariant total — *every state whose RK4
stages are evaluated has been domain-checked, with a one-frame lookahead, before the stages run*.
That sentence goes in the code and in `PLAN.md`'s PR 2c line. (Atmosphere needs no frame-0 twin:
`from_config` already range-checks the initial altitude at init.)

**6. `Vehicle::integrate`'s early return on `!eom_.has_value()` is unchanged and correct** — a
no-physics vehicle has no Earth model, no `p_n_dot`, and no position representation to be out of
domain in. Stated in the code comment rather than worked around.

## Files

**Interfaces** — `core/include/astra/interfaces/{earth,gravity}_model.hpp`,
`core/src/interfaces/{earth,gravity}_model.cpp`
One new non-pure virtual each, ADR-0012's exact signature and comment intent:
`[[nodiscard]] virtual bool in_domain(const Eigen::Vector3d& p_n) const noexcept;` returning `true`
out of line.

**Plugins** — `plugins/physical/earth/{round,wgs84_rotating}/*.cpp`,
`plugins/physical/gravity/{inverse_square,wgs84_j2}/*.cpp`
The first three gain the `domain_fault` helper + `in_domain` override with `validate_p_n` rewritten
over that helper per decision 5; `wgs84_j2` takes the simpler two-line form of decision 5b. `round`'s
floor is `-radius_`; `wgs84_rotating`'s is `-geodesy::meridian_radius(lat)` (the more restrictive of
M/N, per the existing Codex finding); `inverse_square`'s is its **own** configured `radius_` (the
existing Codex finding that earth's floor says nothing about gravity's separately configured one);
`wgs84_j2`'s is `kMinSafeRadius` on the ECEF radius. Only `flat` and `gravity/constant` are untouched
— they divide by nothing and take the base default. The long "NOT verified end-to-end / deliberately
out of scope for this PR" disclosure block in `round.cpp` is replaced by what is now true.

**Monitor** — `core/include/astra/kernel/health_monitor.hpp`, `core/src/kernel/health_monitor.cpp`
- `HealthCheck::kOutOfDomain` added to the enum.
- `domain_action_` parsed from `sim.health`'s `domain` key via the existing `parse_action`, default
  `kHalt`; `domain_count_` registered as `health.domain`.
- `[[nodiscard]] HaltReason report_out_of_domain(std::string_view source, const StepContext& ctx,
  std::uint32_t vehicle_index) noexcept;` — one call through the existing `apply()`.
- `[[nodiscard]] bool domain_enabled() const noexcept { return domain_action_ != HealthAction::kOff; }`

  Named `report_out_of_domain`, not `check_domain`, deliberately: unlike `check_wrench`/`check_state`,
  the **predicate is model-owned** (ADR-0012's own framing) and the monitor owns only the action. The
  callers evaluate their predicates and call this **at most once per vehicle per frame**, so `warn`
  counts one fire per frame exactly like the other checks. `domain_enabled()` exists for cost, not
  correctness: it is what makes "`domain: off` restores current behavior" literally true — no
  `in_domain` call, no extra `p_n_dot`, no `altitude()`.
- **A comment naming the ADR deviation.** ADR-0012 reads "the monitor … checks `earth->altitude(vs)`
  against `atmosphere->valid_range()`", which describes a monitor holding model pointers; this one
  holds none, and keeps only the action, the count and the signal. ADRs are immutable, so a future
  reader diffing ADR-0012 against this header would see an unexplained mismatch. One comment on
  `report_out_of_domain` cites ADR-0012 and states why the split is faithful to it: the ADR's own
  sentence is "a HealthMonitor check over a **model-owned predicate**", and putting the predicate at
  the call site is what makes it model-owned — the alternative would hand `HealthMonitor` an Earth,
  gravity and atmosphere pointer it has never had.

**Vehicle** — `core/include/astra/kernel/vehicle.hpp`, `core/src/kernel/vehicle.cpp`
- `PhysicsSpec` gains `std::string earth_plugin_path; std::string gravity_plugin_path;`
  (`from_config` fills them from `earth_spec`/`gravity_spec`; `for_testing` supplies
  `"for_testing's earth"` / `"for_testing's gravity"`, the existing `"for_testing's wind"` idiom).
  `init()` copies them into two `std::string` members, the same lifetime discipline as
  `state_names_`/`wrench_total_name_`, so `HaltReason::source` points at run-lifetime storage.
- New private `[[nodiscard]] HaltReason check_domain(const StepContext&, HealthMonitor&) noexcept;`
  (factored out to stay under clang-tidy's `readability-function-cognitive-complexity`):

  ```
  p_n = state_'s position; p_next = p_n + ctx.dt * eom_->earth().p_n_dot(VehicleState{state_})
  first offender wins:  earth  fails at p_n or p_next  -> earth_plugin_path_
                        gravity fails at p_n or p_next -> gravity_plugin_path_
  -> health.report_out_of_domain(offender, ctx, index_)
  ```
- `integrate()` calls it twice, both gated on `health.domain_enabled()`:
  - **before the wrench loop when `ctx.frame == 0`** (decision 5c), on the pre-step state; a halt
    returns with `state_` untouched, like a halting wrench check;
  - **immediately after `check_state` and before `post_step_renormalize`**, on the post-step state,
    skipped when `state_halt` already fired.

  Renormalization stays unconditional (ADR-0001), and the state halt still wins over the domain halt.

**Simulation** — `core/include/astra/kernel/simulation.hpp`, `core/src/kernel/simulation.cpp`
- New `AtmosphereModel* atmosphere_ = nullptr;` and `std::string atmosphere_plugin_path_;`, set in
  `from_config` from the existing `LoadedAtmosphere` (today it goes out of scope after the init
  check).
- `step()`: after `vehicle.integrate(...)` returns falsey, if `atmosphere_ != nullptr &&
  health_->domain_enabled()`, compare `earth_->altitude(VehicleState{vehicle.state()})` against
  `atmosphere_->valid_range()` in the same NaN-safe positive form the init check already uses
  (`!(a >= min && a <= max)`, with the existing `readability-simplify-boolean-expr` NOLINT and its
  reason), and `report_out_of_domain(atmosphere_plugin_path_, ...)` on failure. The clock does not
  advance on that halt.
- `for_testing` takes **no** atmosphere parameter; its doc comment's existing list of deliberate
  `from_config` divergences gains this one explicitly, so it is a decision on the record rather than
  a fourth omission found by review.
- `from_config`'s init `validate_p_n` / `valid_range` calls stay exactly as they are, named as
  init-only in their comments (input sanity is init's job).

**`PLAN.md`** — tick the PR 2c checkbox, and rewrite the **three existing disclosure passages** that
currently say the gap is "now scheduled as PR 2c under ADR-0012" to say it is **closed by PR 2c**:
PR 1's polar-approach passage (~L73-75), PR 1b's mid-run atmosphere-range passage (~L88-95), and
PR 2's re-acceptance passage (~L119-122). They are **not deleted** — they are the record of what
shipped and why. Also add decision 5c's frame-0 sentence to the PR 2c line. `/wrap` ticks the
checkbox; it will not rewrite three prose disclosures, so this is done by hand here.

**`HaltReason::source` names the plugin path** for all three — `plugins/physical/earth/round`,
`plugins/physical/gravity/inverse_square`, `plugins/physical/atmosphere/isa1976` — per ADR-0012
("names the offending model's plugin path"), §15, and the two existing checks. Confirmed with Joe.

## Tests

**`tests/unit/earth/earth_kinematics_test.cpp`** (+ gravity) — `in_domain` directly:
true inside the domain; false at `|lat| > kMaxLatitude`, at `alt <= -radius_`, and for a NaN
component; **true for `lon = 4.0` while `validate_p_n` still throws on it** — the ADR's central
split, tested as such. Same for `wgs84_rotating` (floor at `-M(lat)`) and `inverse_square` (its own
`radius_`). For `wgs84_j2`, `in_domain` is false at `docs/lessons.md`'s own numeric case —
`lat = kMaxLatitude`, `alt = -N(lat)*(1-e^2)`, `mag_r ≈ 0.043` m — **while `wgs84_rotating::in_domain`
is true at that same `p_n`**, which is the whole point of it having its own predicate. Only `flat` and
`gravity/constant` return the base `true` everywhere tried. Existing `validate_p_n` message tests must
pass unchanged — that is the no-drift proof.

**`tests/unit/health/health_monitor_test.cpp`** — `domain` parses off/warn/halt, defaults to halt,
rejects a bad string; `health.domain` counts under warn and under halt; `HealthCheck::kOutOfDomain`
and the source survive into `HaltReason`.

**`tests/unit/kernel/simulation_halt_test.cpp`** + new configs under `tests/configs/` — the six
acceptance bullets end to end through `Simulation::from_config`:

| case | fixture | assertion |
|---|---|---|
| polar approach | `earth/round` + `gravity/constant`, lat just inside `kMaxLatitude`, north velocity | halts, `kOutOfDomain`, source `plugins/physical/earth/round`, **and `state().allFinite()` at the halt** — i.e. at least one frame before anything goes non-finite |
| altitude floor | `earth/round`, descending toward `-radius_` | halts naming `plugins/physical/earth/round`, state finite |
| gravity's own floor | `earth/round` default radius + `inverse_square` with a small `radius` | halts naming `plugins/physical/gravity/inverse_square` — earth's floor is nowhere near, so this proves the two are checked independently |
| atmosphere excursion | `earth/flat` + `isa1976`, starting at −4900 m and descending past −5000 m | halts naming `plugins/physical/atmosphere/isa1976`, at the expected frame |
| antimeridian | `earth/round`, lon just under pi, eastward velocity | **no halt**; asserts lon actually passed pi |
| `domain: off` | the polar fixture + `sim.health: { domain: off }` | runs to completion, no halt, lat past `kMaxLatitude` — current behavior restored |

Plus a `warn` variant asserting no halt and `health.domain == 1`, and a **frame-0** case (decision
5c): an initial state that is itself in domain but whose `p_n + dt*p_n_dot` is not, halting at
`frame == 0` with the state untouched — the one case the post-step check alone cannot see.

**`tests/physics/no_alloc_test.cpp`** + `tests/configs/no_alloc_domain.yaml` — a third case:
`earth/wgs84_rotating` + `gravity/wgs84_j2` + `isa1976`, 1000 frames, zero allocations with the check
active. Deliberately the most expensive pairing: the lookahead `p_n_dot`, four `in_domain` calls
(two of which each run `geodetic_to_ecef`), and the post-step `altitude()`, all on the per-frame
path. Initial conditions are chosen comfortably inside every domain for the full 1000 frames (mid
latitude, well clear of `kMaxLatitude`; an altitude and velocity that cannot reach the `M(lat)`/ECEF
floors or leave `[-5000, 86000]` m in 1 s of flight) so the test asserts **both** `after == before`
**and** `sim.clock().frame() == 1000` with a falsey `HaltReason` — not allocation count alone, since
a fixture that halts early would pass the allocation assertion having measured far fewer frames than
it claims.

`tests/unit/CMakeLists.txt` needs no new entry unless I add a separate `kernel/domain_test.cpp`; new
configs are data, picked up by `ASTRA_TEST_CONFIG_DIR`.

## Not in scope

PR 1b's WGS-84/ISA **geoid datum** gap — a wrong reference surface, not a domain excursion
(ADR-0012's own Consequences). It stays disclosed, and Part A's PLAN.md edit makes it load-bearing
for PR 3.

---

## Verification

`just check` is the gate and the definition of done. Per `CLAUDE.md`:

1. `just check > /tmp/.../check.log 2>&1; echo "EXIT=$?" >> ...` launched with the Bash tool's own
   `run_in_background` — never `nohup`/`&`/`disown` inside the command.
2. Find the real pid from `ps aux`'s full command column: the line whose command is exactly
   `just check`, excluding any `bash -c` wrapper. `pgrep -x just` has returned a decoy pid here
   before (`docs/lessons.md` 2026-09-16). Confirm that pid is alive.
3. Block on it: `tail --pid=<pid> -f /dev/null`.
4. Read `EXIT=0` / `check: OK` from the log's tail. **Nothing is verified before that line is read.**

Single-test iteration while working: `ctest --preset asan -R <name>`.

## Commit, review, PR

1. Part A as its own docs-only commit (guard armed; `git ls-files -s AGENTS.md` verified `120000`).
2. Part B, then `git add -A` and **commit before review** — Codex's `git diff` never shows untracked
   files and this PR adds new tests and configs.
3. `node "$(find ~/.claude/plugins -name codex-companion.mjs | head -1)" review --base origin/main --wait`
4. `adversarial-review --base origin/main --wait` with focus naming: **ADR-0012** (post-step plus
   lookahead, not per-stage; plus the two named deviations — the predicate lives at the call site
   rather than in a monitor holding model pointers, and frame 0 also checks pre-step); **ADR-0003**
   (`p_n`'s meaning is the Earth model's; the init-only input-sanity checks stay init-only, by name);
   **ADR-0004** (the derivative functor is unchanged); **§14** (no per-frame allocation or exceptions
   — the check is an observation, not a throw); **§15** (the halt names the offending plugin).
5. Physics diff → Joe reads it. Push the branch, open the PR, Joe squash-merges.
6. `/wrap`.
