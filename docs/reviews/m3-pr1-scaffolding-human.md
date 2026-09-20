# Human review packet: M3 PR 1 — aircraft/_template + aircraft/notional_transport scaffolding

PR / candidate base and head: `main` at `91aa358` (M3 PR 0, #36); committed candidate `fc3b6d7`
(main edit set) + `8e0cfe4` (session-handoff commit) on branch `worktree-m3-pr1-scaffolding`, PR
<https://github.com/jhawleyjr/ASTRA/pull/37>, plus this round's own fix delta (J-001..J-004) on
top, not yet committed as of packet preparation. `main` has not advanced since this candidate's
base was taken.
Spec/plan/task: `PLAN.md`'s M3 PR 1 line; Tier 1 brief agreed in-chat (no `docs/plans/` file),
refined through your correction rounds on O1's numbers/convention and the four scaffolding
directions (numbers-in-YAML, anonymous-namespace discipline, no aero/turbofan dirs yet, §13
condensed from `cruise.yaml` with keys/values identical) and the O1 source switch to JSBSim's
`docs/refs/aircrafts/737.xml`.
Risk: Tier 1 per `PLAN.md`'s own PR 1 line ("Tier 1. Plan critique: no"). No new ADR (binding:
ADR-0009, already accepted).
Decision requested: approve behavior and authorize commit + draft PR (this session does not push
or open a PR without that authorization).

## What changes and why

Creates `aircraft/` for the first time: `aircraft/_template/` and `aircraft/notional_transport/`,
each with a real `physical/mass` `MassPropertiesModel` plugin and a `configs/cruise.yaml`. No
`aero`/`hardware/propulsion/turbofan` directories yet — those are PR 3/4 scope and aren't created
at all here (not even empty), per your explicit direction. Adds `just new-aircraft <name>`
(`scripts/new-aircraft.sh`), rewrites `docs/architecture.md` §7/§13/§16 and three `PLAN.md` lines
(PR 1's O1 citation implicit in the shipped files, PR 4's O2/O3 transcription note, PR 7's S&L
§4.2 sanity-check framing replacing the "vs. O4" wording), and adds a `PLAN.md` follow-up bullet
flagging `plugins/physical/mass/constant`'s undocumented `ixz` sign convention (not touched here).

Diff stat (tracked files): `PLAN.md` +34/-8 across three edits, `docs/architecture.md` +53/-19
across three sections, `justfile` +10 (new recipe), `cmake/AstraPlugin.cmake` +3/-1 (plugin
build-dependency fix, see Independent review), `tests/unit/CMakeLists.txt` +22/-1. Untracked (new)
files: 6 under `aircraft/` (two plugins + two configs, two `CMakeLists.txt`), `scripts/
new-aircraft.sh`, `tests/python/test_new_aircraft.py`, three new C++ test files under
`tests/unit/`, and this candidate's two `docs/reviews/` records.

**O1 (mass/inertia), transcribed not proposed:** `aircraft/notional_transport`'s `physical/mass`
config carries mass 37,648 kg, Ixx 761,970 kg·m², Iyy 1,997,120 kg·m², Izz 2,567,919 kg·m², Ixz
−10,846.5 kg·m² (S&L product-of-inertia convention) — all transcribed from JSBSim's "737" example
aircraft's `<mass_balance>` block (`docs/refs/aircrafts/737.xml`, gitignored, not in this repo;
$Revision: 1.43 $, Dave Culp/Aeromatic, GPL; that file's own note: "created using ... guesses ...
educational and entertainment purposes only" — not a validated real 737). The resulting principal
axis tilts ~0.34° NOSE-UP — what the source's `negated_crossproduct_inertia="true"` `<ixz>` token
actually gives once converted to our tensor convention, not a generic textbook nose-down
assumption; verified against `docs/refs/aircrafts/737.xml`'s own primary source (`FGMassBalance.cpp`
read directly, not summarized) during the brief.

**Sign-convention design (both new mass plugins):** config keys `ixy`/`ixz`/`iyz` are S&L-style
products of inertia (`integral(x*z*dm)`, positive-definition); each plugin negates them before
writing `inertia_b`'s off-diagonals, documented in the plugin's own header comment and in
`cruise.yaml`'s inline comments. `plugins/physical/mass/constant` (existing, unchanged) uses the
opposite convention — places its config value directly into the tensor, unnegated — undocumented
there; flagged as a `PLAN.md` follow-up, not fixed in this PR.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| `just check` (initial candidate) | pass, `check: OK`, exit 0 | all 4 presets, clang-tidy, pyright 0 errors, pytest 36 passed |
| `just format-check` failure → `just format` → rerun | 1 formatting-only failure, fixed | clang-format violations in the two new mass test files only |
| `just check` (post-format) | pass, `check: OK`, exit 0 | same scope as above |
| `just check` (post-fix-batch, current candidate) | pass, `check: OK`, exit 0 | pytest 38 passed (36 + 2 new regression tests for R-002) |
| `ctest -L unit -R notional_transport_mass` | 3/3 pass | `NotionalTransportMass.{MassAndDiagonalInertiaMatchTheTranscribedSource, ConfigIxzIsTheNegatedTensorEntry, PrincipalAxisTiltMatchesTheTranscribedSourceNotAnAssumedValue}` |
| `ctest -L unit -R template_mass` | 1/1 pass | `TemplateMass.ZeroedConfigThrowsConfigErrorNamingTheTodoField` |
| `ctest -L unit -R NotionalTransportCruiseConfig` | 1/1 pass | loads `aircraft/notional_transport/configs/cruise.yaml` via `Simulation::from_config`, runs 1000 frames, no halt |
| `pytest tests/python/test_new_aircraft.py` | 6/6 pass | copy+rewrite, 3 rejection cases, justfile-quoting regression, real-`just` injection regression |
| Manual: `just new-aircraft scratch_evidence_test` (real run, this worktree) | succeeded, config paths rewritten correctly | scratch directory removed after, not committed |
| Manual: `ASTRA_STATIC_PLUGINS=ON` build with `_template` + `notional_transport` + the scratch aircraft all present | linked cleanly, no ODR collision | `cmake --preset static-plugins && cmake --build --preset static-plugins`, ninja output retained in session transcript |
| Manual: isolated single-target build (Codex's R-001 repro) | `rm -rf build/asan && cmake --preset asan && ninja -C build/asan tests/unit/astra_notional_transport_mass_test` built every plugin first, 3/3 tests passed | confirms the `cmake/AstraPlugin.cmake` fix |
| `just docs-check` (after adding the two `docs/reviews/` files) | pass, "24 Markdown files checked" | `git diff --check` also clean |
| `just check` (round 2, after Joe's PR review fixes J-001..J-004) | pass, `check: OK`, exit 0 | pytest 38 passed (no new tests this round, existing ones fixed) |
| Manual: `notional_transport_mass_test` rerun with `ixz` sign flipped directly in the shipped `cruise.yaml` (reverted before commit) | 2/3 tests failed as intended | confirms J-002's fix actually pins the shipped file, not a fixture copy |

Numerical evidence for O1's transcription: unit-conversion factors and the resulting
principal-axis tilt are shown as a derivation, not a bare pass/fail — see `docs/reviews/
m3-pr1-scaffolding-review.md`'s scope note and `aircraft/notional_transport/configs/cruise.yaml`'s
own inline comments for the full arithmetic (LBS→kg ×0.45359237, SLUG·FT²→kg·m² ×1.35581795).

## Independent review

Builder: Claude (Sonnet 5). Reviewer: Codex (`codex-companion` 1.0.6), fresh context, three
dispatches — general review, adversarial review (physics/sign-convention/ODR/config/script focus),
and a fix-verification adversarial pass. Full findings: `docs/reviews/m3-pr1-scaffolding-review.md`.

Three findings, all fixed and Codex-verified (verdict "approve", "New findings: none" from the
verification pass's own fix-delta/blast-radius inspection):
- **R-001** (major): the two new standalone test executables loading a real plugin weren't wired
  into `astra_finalize_plugins()`'s explicit plugin-build-dependency list, so a from-clean,
  single-target build (not `all`) could run against a missing/stale plugin. Fixed by adding both
  targets to that list; reproduced the failure pre-fix and the correct behavior post-fix.
- **R-002** (medium): the `new-aircraft` justfile recipe interpolated `{{name}}` into the shell
  command text unescaped — `just new-aircraft '$(cmd)x'` would execute `cmd` via the recipe's own
  shell before the script's own regex validation ever ran. Fixed with `just`'s `quote()` builtin;
  two regression tests added, one of which exercises the real `just` + justfile end-to-end (the
  isolated-temp-root tests structurally couldn't catch this).
- **R-003** (medium): `new-aircraft.sh`'s separate existence-check-then-copy was a TOCTOU race
  that could nest `_template` inside a partially-created aircraft directory, and left a
  half-populated directory on any mid-copy failure. Fixed with an atomic `mkdir` claim, copying
  `template/.` (contents, not the directory itself) into the pre-created destination, and an `ERR`
  trap cleanup.

**Round 2**, from your own direct review of PR #37, four more findings (J-001..J-004), all fixed
and Codex-verified the same way (verdict "approve", "New findings: none"):
- **J-001** (minor): §13/the packet/the PR body claimed the embedded example was `cruise.yaml`
  "verbatim" — it wasn't (comments trimmed). Reworded to "condensed... keys/values identical"
  rather than adding a permanent docs-check for one illustrative embed.
- **J-002** (major): `notional_transport_mass_test` built its config from an inline fixture
  string duplicating O1's numbers, so it couldn't catch a future edit to the real shipped
  `cruise.yaml`. Now reads `physical.mass` from that real file; confirmed a flipped `ixz` sign in
  the shipped file now fails the test.
- **J-003** (minor): the mass plugins' `PluginDescriptor` name (`"template_mass"`/
  `"notional_transport_mass"`) would go stale forever on every future aircraft copy, since
  `new-aircraft.sh` never touches `.cpp` source. Both now use a copy-invariant generic `"mass"`.
- **J-004** (minor, process): recorded a `PLAN.md` follow-up for `astra_finalize_plugins()`'s
  hardcoded test-target list conflicting with §18's "no framework edit" on-ramp story — an
  `astra_add_plugin_test()` helper, not implemented in this PR.

No critical/major findings remain open. No findings rejected or deferred.

## Joe's attention

- **O1's five numbers and the Ixz sign convention** (mass/Ixx/Iyy/Izz already agreed; Izz's 0.97
  correction factor and the JSBSim transcription/sign-derivation for Ixz were the last two rounds
  of correction) — these are the numbers that will anchor O2/O3's aero/propulsion transcription in
  PR 4 and the trim/mode checks in PR 6/7, so an error here propagates forward.
- **The R-003 concurrency fix was not exercised with an actual concurrent-race reproduction** by
  either builder or reviewer — verified by inspection of `mkdir`'s POSIX atomicity guarantee and
  by the (necessarily serial) test suite. If you want an actual race test, that's a follow-up, not
  a blocker for a scaffolding script two people are unlikely to invoke simultaneously.
- **`plugins/physical/mass/constant`'s own `ixz` convention is still undocumented** (flagged as a
  `PLAN.md` bullet, not fixed) — a pre-existing gap this PR's own two new plugins deliberately did
  *not* inherit, by using the opposite (documented) convention instead.
- No ADR changes, no ICD changes, no golden/oracle files touched. Rollback: this candidate is
  additive-only (new `aircraft/` tree, one new justfile recipe, one CMake dependency-list edit) —
  reverting is a straightforward revert of the single commit once one exists.

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: not requested

Significant tradeoff rationale: none beyond what's already recorded in the brief (Izz's 0.97
factor, Ixz's convention choice, dropping the illustrative `hardware.propulsion`/hand-wavy `sim.
transport` config keys from §13 in favor of only-real-keys).
Accepted risks: R-003's concurrency fix is inspection-verified, not race-tested (owner: whoever
next touches `new-aircraft.sh`, trigger: any report of a corrupted `aircraft/<name>` directory).
`plugins/physical/mass/constant`'s undocumented `ixz` convention (owner: unassigned, trigger:
PLAN.md's own follow-up bullet, next touched when that plugin is next modified).
Nonblocking follow-ups: the two accepted risks above; the `PLAN.md` follow-up bullet is itself the
tracking item for the second one.
Architectural decisions: none new; ADR-0009 (accepted) is the binding architecture for this PR.
