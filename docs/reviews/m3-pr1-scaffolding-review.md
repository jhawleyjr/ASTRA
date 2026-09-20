# Review: M3 PR 1 — aircraft/_template + aircraft/notional_transport scaffolding

Spec/plan: `PLAN.md`'s M3 PR 1 line; brief agreed in-chat (no `docs/plans/` file, Tier 1, no plan
critique), including the O1 mass/inertia numbers Joe approved (JSBSim `docs/refs/aircrafts/
737.xml` transcription) and the four scaffolding directions from his correction rounds.
Builder: Claude (Sonnet 5)
Reviewer: Codex (general review + adversarial review, both `codex-companion` 1.0.6, fresh context)
Scope: uncommitted working-tree diff against `main` at `91aa358` (M3 PR 0, #36) — no commits yet
on this branch. Touches `PLAN.md`, `docs/architecture.md`, `justfile`, `cmake/AstraPlugin.cmake`,
`tests/unit/CMakeLists.txt`, plus new files under `aircraft/_template/`,
`aircraft/notional_transport/`, `scripts/new-aircraft.sh`, `tests/python/test_new_aircraft.py`,
`tests/unit/mass/`, `tests/unit/kernel/notional_transport_cruise_config_test.cpp`.
Validation: `just check` green (all four presets, clang-tidy, pyright 0 errors, pytest 36 passed)
before dispatch, and again after the fix batch below. Manual evidence: `just new-aircraft
scratch_evidence_test` run for real in this worktree, `ASTRA_STATIC_PLUGINS=ON` build proving
`_template`/`notional_transport`/the scratch aircraft's three `physical/mass` plugins link into
one binary with no ODR collision, then the scratch directory removed (not committed).
Round: initial

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

## Completion

Zero critical findings. Three major/medium findings, all fixed and verified (Codex
fix-verification adversarial-review pass, `codex-companion` 1.0.6, same working-tree diff,
verdict "approve": all three hold, "New findings: none" from its own fix-delta/callers/invariants
inspection). None rejected or deferred. No scope was left unreviewed within the three dispatched
passes' own stated focus. `just check` reran green after the fix batch (all four presets,
clang-tidy, pyright 0 errors, pytest 38 passed). Remaining risk: the R-003 concurrency fix was
verified by inspection (mkdir's POSIX atomicity guarantee) and non-concurrent test coverage, not
an actual two-process race reproduction, by either builder or reviewer. Proceeding to
`/human-review`; Codex's approval is not Joe's approval.
