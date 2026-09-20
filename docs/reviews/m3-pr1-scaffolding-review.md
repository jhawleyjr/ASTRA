# Review: M3 PR 1 — aircraft/_template + aircraft/notional_transport scaffolding

Spec/plan: `PLAN.md`'s M3 PR 1 line; brief agreed in-chat (no `docs/plans/` file, Tier 1, no plan
critique), including the O1 mass/inertia numbers Joe approved (JSBSim `docs/refs/aircrafts/
737.xml` transcription) and the four scaffolding directions from his correction rounds.
Builder: Claude (Sonnet 5)
Reviewer: Codex (general review + adversarial review, both `codex-companion` 1.0.6, fresh context)
for round 1 (R-001/R-002/R-003); Joe (direct PR review, PR #37) for round 2 (J-001..J-004),
Codex re-verification requested for round 2's delta.
Scope round 1: uncommitted working-tree diff against `main` at `91aa358` (M3 PR 0, #36). Scope
round 2: committed candidate `fc3b6d7`/`8e0cfe4` on branch `worktree-m3-pr1-scaffolding` (PR #37)
plus this round's own uncommitted fix delta, same base `91aa358`. Touches `PLAN.md`,
`docs/architecture.md`, `justfile`, `cmake/AstraPlugin.cmake`, `tests/unit/CMakeLists.txt`, plus
new files under `aircraft/_template/`, `aircraft/notional_transport/`, `scripts/new-aircraft.sh`,
`tests/python/test_new_aircraft.py`, `tests/unit/mass/`,
`tests/unit/kernel/notional_transport_cruise_config_test.cpp`.
Validation: `just check` green (all four presets, clang-tidy, pyright 0 errors, pytest 36 passed)
before round-1 dispatch, and again after the round-1 fix batch (38 passed). Manual evidence: `just
new-aircraft scratch_evidence_test` run for real in this worktree, `ASTRA_STATIC_PLUGINS=ON` build
proving `_template`/`notional_transport`/the scratch aircraft's three `physical/mass` plugins link
into one binary with no ODR collision, then the scratch directory removed (not committed). Round 2
adds: `astra_notional_transport_mass_test` rebuilt and run directly, confirming it now reads
`aircraft/notional_transport/configs/cruise.yaml` and that flipping `ixz`'s sign in that real file
(temporary local edit, reverted before commit) fails 2 of 3 tests as intended; full `just check`
rerun pending below.
Round: fix 2 (round 1: R-001/R-002/R-003, Codex-verified, closed; round 2: J-001..J-004, Joe's
direct PR review)

## Finding R-001

Severity: major
Status: verified
Location: `tests/unit/CMakeLists.txt:57-60` (new `astra_notional_transport_mass_test` /
`astra_template_mass_test` targets); root cause in `cmake/AstraPlugin.cmake`'s
`astra_finalize_plugins()`
Requirement/ADR: ADR-0009 (plugin discovery/build); PLAN.md M3 PR 1's own accept criterion
`ctest -L unit -R notional_transport_mass`
Failure: `astra_finalize_plugins()` wires a plugin build-order dependency (`add_dependencies`)
onto exactly two hardcoded targets, `astra_unit_tests` and `astra_physics_tests` — by design
("listed explicitly ... so a new test target opts in", per that function's own comment). The two
new standalone test executables that load a real plugin via `PluginLoader` were not added to that
list. Reproduced: `ninja -C build/asan -t query tests/unit/astra_notional_transport_mass_test`
showed no dependency edge on either mass plugin's `.so`; a clean, targeted
`ninja tests/unit/astra_notional_transport_mass_test` (not a full/`all` build) linked and ran
against a stale-or-missing plugin, which would surface as a `PluginError` at `loader.load()`
rather than a real code defect.
Requested outcome: both new targets get the same plugin build-order dependency
`astra_unit_tests`/`astra_physics_tests` already have.
Builder response: fixed. Added `astra_notional_transport_mass_test` and `astra_template_mass_test`
to `astra_finalize_plugins()`'s `foreach` list (`cmake/AstraPlugin.cmake`).
Verification: reproduced the exact failure mode pre-fix (`ninja -t query` showed no plugin edge),
then confirmed post-fix: `rm -rf build/asan && cmake --preset asan && ninja -C build/asan
tests/unit/astra_notional_transport_mass_test` (a from-clean, single-target build, not `all`)
built every plugin first and the executable's 3 tests passed. Codex (adversarial, fix-verification
pass, same working-tree diff): "both mass-test targets receive plugin dependencies after target
creation; static builds retain dependencies through astra_core" — verified by static inspection,
runtime results not independently reproduced.

## Finding R-002

Severity: medium
Status: verified
Location: `justfile`'s `new-aircraft` recipe (pre-fix: line ~104, `./scripts/new-aircraft.sh
{{name}}`)
Requirement/ADR: none binding; general shell-injection hygiene for a scaffolding tool a user runs
directly
Failure: `just` substitutes a recipe parameter into the recipe's command text before bash parses
it. With a bare `{{name}}`, `just new-aircraft '$(touch /tmp/x)glider'` causes the recipe's own
shell to command-substitute and execute `touch /tmp/x` before `scripts/new-aircraft.sh` ever
receives an argument — the script's own `^[a-z0-9_]+$` regex validation runs too late to matter,
since the injection already happened one layer up. `tests/python/test_new_aircraft.py`'s existing
tests invoked the script directly, bypassing this entry point entirely and missing it.
Requested outcome: the name must reach the script as inert text regardless of its shell
metacharacter content.
Builder response: fixed. Recipe now reads `./scripts/new-aircraft.sh {{quote(name)}}` —
`quote()` is `just`'s builtin shell-escaping function (verified empirically: single-quote
metacharacters and embedded-single-quote breakout attempts are both neutralized).
Verification: reproduced pre-fix (`$(touch /tmp/astra-injection-probe)glider` created the sentinel
file through the raw `just` recipe); confirmed post-fix the same input is now rejected by the
script's own regex with no side effect (`ls` on the sentinel path fails). Added two regression
tests: `test_justfile_recipe_quotes_the_name_argument` (pins the exact recipe text) and
`test_just_recipe_rejects_a_shell_injection_shaped_name` (end-to-end through the real `just` +
justfile + script, not the isolated-temp-root tests, which structurally cannot exercise this
entry point). Codex (adversarial, fix-verification pass, same working-tree diff): "quote(name)
passes shell metacharacters as inert argument text to the script's validation" — verified.

## Finding R-003

Severity: medium
Status: verified
Location: `scripts/new-aircraft.sh` (pre-fix: separate `[[ -e "$dest" ]]` check then `cp -r
aircraft/_template "$dest"`)
Requirement/ADR: none binding
Failure: two independent problems from the same non-atomic check-then-copy shape. (1) TOCTOU: two
concurrent invocations for the same name can both pass the existence check before either copies;
`cp -r template dest` when `dest` already exists as a directory copies `template` AS A
SUBDIRECTORY of `dest` (a well-known `cp` behavior) rather than erroring, so the second invocation
nests `aircraft/_template`'s own tree inside the first's aircraft directory — a nested
`_template` that `astra_discover_plugins(aircraft)`'s recursive `CMakeLists.txt` glob would then
walk into. (2) a `cp`/`sed` failure partway through (disk full, permission error) leaves a
half-populated aircraft directory that blocks every future retry (the existence check now finds
something there) and that a build could still discover in its broken state.
Requested outcome: the destination path must be claimed atomically, and any failure after
claiming it must clean up rather than leave a partial directory.
Builder response: fixed. Replaced the check with `mkdir -- "$dest"` (a single atomic syscall:
succeeds for exactly one racing invocation, fails immediately with EEXIST for the rest, and
rejects a pre-existing file or symlink at that path too, not just a directory) and switched the
copy to `cp -r aircraft/_template/. "$dest/"` (copies contents into the already-created directory,
so there is no path through which `cp` could nest `_template` regardless of timing). A `trap 'rm
-rf -- "$dest"' ERR`, cleared only after both the copy and the config rewrite succeed, removes a
partially-populated `$dest` on any failure in between.
Verification: existing `tests/python/test_new_aircraft.py` tests (copy, rewrite, three rejection
cases) all still pass against the rewritten script. Did not construct an actual concurrent-race
test (a genuine two-process race is inherently flaky to assert on); the fix's correctness rests on
`mkdir`'s POSIX atomicity guarantee and the `template/.`-vs-`template` copy-target distinction,
both verified by inspection and by the passing non-concurrent test suite exercising the same code
path. Codex (adversarial, fix-verification pass, same working-tree diff): "mkdir exclusively
claims the destination before cleanup is armed; copying template/. prevents nesting, and
cp/find/sed command failures trigger cleanup" — verified by static inspection, same
not-independently-reproduced caveat (no actual concurrent-race repro run by either party).

## Finding J-001

Severity: minor
Status: verified
Location: `docs/architecture.md` §13 (the sentence introducing the config example);
`docs/reviews/m3-pr1-scaffolding-human.md`'s "What changes and why"; PR #37's body Summary bullet
Requirement/ADR: none binding; accuracy of a documentation claim
Failure: all three places asserted the embedded example was `cruise.yaml` "verbatim." It isn't —
the shipped file's explanatory comments (the citation block, the `ixz` sign-derivation block) are
trimmed from the doc's embed for length. Keys and values are identical, but "verbatim" overclaims.
Requested outcome: either the claim is made true (a docs-check enforcing key/value equality
between the embed and the file) or the wording is corrected to what's actually true. Joe left the
choice to the builder.
Builder response: fixed by rewording, not by adding a check. A docs-check parsing a fenced YAML
block out of Markdown and diffing it key-by-key against a real file is real, permanent machinery
to maintain for a single illustrative embed; rewording is proportionate to a Tier 1 scaffolding
PR and just as honest. §13 now reads "condensed from `aircraft/notional_transport/configs/
cruise.yaml` ... comments trimmed for length, but every key and value identical to the shipped
file." The human-review packet and PR body updated to match.
Verification: `git grep -n verbatim` across `docs/` and the PR body (via `gh pr view --json body`)
confirms no remaining false "verbatim" claim about this example; a line-by-line comparison of the
embed against the current `cruise.yaml` (done by hand while rewriting) confirms every key/value
pair is in fact still identical, which is what makes "condensed" accurate rather than another
overclaim in the other direction. Codex (adversarial, round-2 fix-verification pass): "architecture
wording explicitly says comments are trimmed with identical keys/values; the human packet says
condensed with identical keys/values... direct comparison confirms all active YAML keys and values
match cruise.yaml" — verified.

## Finding J-002

Severity: major
Status: verified
Location: `tests/unit/mass/notional_transport_mass_test.cpp` (pre-fix: `load()` built its `Config`
from a hand-written inline string duplicating O1's numbers)
Requirement/ADR: none binding; test-coverage integrity
Failure: the test's fixture numbers were a second, independent copy of O1's values, not read from
the shipped `aircraft/notional_transport/configs/cruise.yaml`. A future edit that flipped `ixz`'s
sign (or any other value) in the real, shipped config file would not be caught by this test at
all — it would keep passing against its own stale copy while the actual aircraft a user runs
would silently carry the wrong tensor.
Requested outcome: the test must read `physical.mass`'s config from the real, shipped file, so an
edit to that file is what the test actually exercises.
Builder response: fixed. `load()` now does `Config::from_file(std::string{ASTRA_AIRCRAFT_DIR} +
"/notional_transport/configs/cruise.yaml").at("physical.mass")` and passes that sub-config
straight to `init()` — the leftover `plugin:` key in that sub-tree is never read by `init()` and
never triggers an unconsumed-key check here (this test calls the model's `init()` directly, the
same way `tests/unit/gravity/inverse_square_test.cpp`'s own `load()` helper does, never going
through `Simulation::from_config`'s separate unknown-key enforcement). Added the
`ASTRA_AIRCRAFT_DIR` compile definition to this test's own target in `tests/unit/CMakeLists.txt`
(the main `astra_unit_tests` target already had it from PR 1's earlier `NotionalTransportConfig`
test; this standalone target did not).
Verification: rebuilt and ran the test against the real file (3/3 pass, same tilt value as
before, confirming the file and the old inline fixture agreed). Then temporarily flipped `ixz`'s
sign directly in the shipped `cruise.yaml` (reverted before commit, never staged) and reran: 2 of
3 tests failed exactly as expected (`ConfigIxzIsTheNegatedTensorEntry` and the tilt test), proving
the test now actually depends on the shipped file's content. Codex (adversarial, round-2
fix-verification pass): "the target defines ASTRA_AIRCRAFT_DIR correctly; load() uses
Config::from_file and config.at(\"physical.mass\"), with no inline fixture remaining. The leftover
plugin key cannot trigger rejection: neither Config nor this direct init() path enforces unknown
keys" — verified.

## Finding J-003

Severity: minor
Status: verified
Location: `aircraft/_template/physical/mass/mass.cpp` and
`aircraft/notional_transport/physical/mass/mass.cpp` (`kDescriptor`'s `name` field: pre-fix
`"template_mass"` / `"notional_transport_mass"`)
Requirement/ADR: ADR-0009's on-ramp story (copy `_template`, edit, rebuild)
Failure: `scripts/new-aircraft.sh` only rewrites config YAML plugin-path strings on copy; it never
touches `.cpp` source. A per-aircraft descriptor name baked into the source as a string literal
therefore never updates when an aircraft is copied from `_template` (or from `notional_transport`)
— every future aircraft's own mass plugin would report `PluginDescriptor.name == "template_mass"`
in its run logs forever, actively wrong and misleading about which aircraft it belongs to.
Requested outcome: a descriptor name that stays correct across every copy, i.e. one that doesn't
encode the aircraft at all (the plugin *path* already does that).
Builder response: fixed. Both files now use the generic `"mass"` for `kDescriptor`'s `name` field
(matching `MassPropertiesModel::name()`'s own already-generic `"mass"`), with a comment in each
file explaining why a per-aircraft name would go stale on copy.
Verification: `git grep -n 'kDescriptor{"physical"'` across `aircraft/` shows both copies now
identical on this field; rebuilt both plugins and reran `notional_transport_mass_test`/
`template_mass_test` (neither asserts on the descriptor name, so no test needed updating) — 3/3
and 1/1 pass respectively. Codex (adversarial, round-2 fix-verification pass): "both descriptors
use \"mass\"; no tests assert the old aircraft-specific names, and loader resolution remains
path-based" — verified.

## Finding J-004

Severity: minor (documentation/process)
Status: verified
Location: `PLAN.md` (new follow-up bullet, top-of-file unchecked-items list)
Requirement/ADR: docs/architecture.md §18's on-ramp story
Failure: not a code defect. `cmake/AstraPlugin.cmake`'s `astra_finalize_plugins()` hardcodes which
test executables receive a plugin build-order dependency; R-001's fix (this round's own PLAN.md
history) added `astra_notional_transport_mass_test`/`astra_template_mass_test` to that explicit
list. This means a future aircraft with its own dedicated test executable needs a framework edit
(`cmake/AstraPlugin.cmake` itself) to build correctly from a clean tree — in tension with §18's
"nothing in the framework is edited" on-ramp claim.
Requested outcome: record this as a tracked follow-up (an `astra_add_plugin_test()` helper,
mirroring `astra_add_plugin()`'s own self-registration), not fixed in this PR.
Builder response: added the bullet to `PLAN.md`'s unchecked-items list, same style as the existing
R-001-era mass/constant convention bullet, immediately following it.
Verification: `git grep -n astra_add_plugin_test` confirms the bullet exists and is the only
occurrence (not yet implemented, as intended — this is a tracking entry, not a fix). Codex
(adversarial, round-2 fix-verification pass): "PLAN.md contains the unchecked
astra_add_plugin_test() follow-up; it changes no code behavior" — verified.

## Completion

**Round 1** (R-001/R-002/R-003): zero critical findings, three major/medium, all fixed and
Codex-verified (verdict "approve", "New findings: none"). None rejected or deferred.

**Round 2** (J-001..J-004, from Joe's direct review of PR #37): zero critical findings, one major
(J-002, a test-coverage gap — the mass test wasn't actually pinning the shipped file), three
minor. All four fixed by the builder and Codex-verified (adversarial fix-verification pass,
`codex-companion` 1.0.6, working-tree diff atop `8e0cfe4`, verdict "approve", "New findings:
none"). `just check` reran green after the fix batch (all four presets, clang-tidy, pyright 0
errors, pytest 38 passed). None rejected or deferred.

Remaining risk carried from round 1: R-003's concurrency fix was verified by inspection (`mkdir`'s
POSIX atomicity guarantee) and non-concurrent test coverage, not an actual two-process race
reproduction, by either builder or reviewer. Codex's approval is not Joe's approval; Joe's own
decision on PR #37 (post round-2 verification) is the outstanding gate.
