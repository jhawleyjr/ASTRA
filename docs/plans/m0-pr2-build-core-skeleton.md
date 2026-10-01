# M0 PR 2 — build system, core skeleton, plugin loader, ICD, hook test runner, CI

Recovered from `~/.claude/plans/read-state-md-plan-md-claude-md-virtual-wilkinson.md` on 2026-09-30. Approval: the file records none (written for Joe's approval); the work merged as #2. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

PR 1 landed the bootstrap files and ADR-0001…0011; there is still no code and nothing to run. PR 2 is the
build: `just check` must exist and be green on an empty core plus one dummy plugin, so that every later
milestone has a gate. Acceptance is architecture §16 row M0 minus the LSP, Python, and skills items (PR 3/4):

1. Core and one dummy plugin build under every preset.
2. The loader loads the dummy by plugin path, rejects a bad `core_api_version` with a typed error naming the
   plugin, and the `ASTRA_STATIC_PLUGINS` build passes the same test without `dlopen`.
3. `icd/astra_icd.h` static asserts hold under clang-18 C++23 and `gcc -std=c11`.
4. The hook test runner in `.claude/hooks/tests/` passes all cases under `just check`.
5. GitHub Actions runs `just check` on `pull_request` on `ubuntu-24.04`, job named `check`.

Then push the branch, open the PR, and do what `/wrap` will do once it exists in PR 4 (rewrite STATE.md,
tick PLAN.md, append lessons if anything bit).

## Restatement of §4 and §10 (checkpoint)

**Plugin model (§4, ADR-0009).** One CMake invocation builds `libastra_core.so` (interfaces, kernel, loader,
registry, config; knows no aircraft or hardware) plus one `.so` per plugin, where a plugin is one thing a
config can select. Plugins are discovered by the build from every `CMakeLists.txt` under `plugins/**`
(shared, by kind: `physical/`, `hardware/`, `sim/`) and `aircraft/<name>/**` (one aircraft's own models,
same kinds, copied from `_template/`). Each plugin's CMakeLists is the one-liner `astra_add_plugin(SOURCES ...)`,
which derives the plugin path from its directory, sets output name, `-fvisibility=hidden`, link to core, and
registers it for the static build and the load-all test. A plugin exports exactly one C symbol,
`astra_plugin()`, returning `PluginDescriptor {category, name, core_api_version, built_with, create}`. The
loader maps a config path such as `plugins/physical/atmosphere/isa1976` to
`libastra_plugins_physical_atmosphere_isa1976.so` in the build's plugin directory, `dlopen`s it at init only,
checks `core_api_version` and `built_with` against its own, and raises a typed init error naming the plugin
on mismatch. `ASTRA_STATIC_PLUGINS` links every plugin into one binary and registers the same descriptors by
direct call, with no second code path. Nobody ships prebuilt `.so` files; integrators are core, not plugins.

**ICD boundary (§10, ADR-0006).** `icd/astra_icd.h` is C11 including only `<stdint.h>`: `ASTRA_ICD_VERSION`,
`ASTRA_ICD_MAGIC` ("ASTR"), `astra_sim_to_sw_v1` (200 bytes) and `astra_sw_to_sim_v1` (64 bytes), fixed-width
fields, 8-byte natural alignment, magic and version in every frame, sizes pinned by static asserts. A layout
is never edited; growth adds a `_vN` struct and bumps the version. It is the only thing the flight software
shares with the sim: `embedded/` links `icd/` + `embedded/hal/`, never `astra_core`. Sim-side, `Transport`
(`send`, `recv`, lockstep `exchange`) is a plugin under `plugins/sim/transport/` (shm for v1, loopback for
tests, FPGA later) with its own divisor (default 10 → 100 Hz) and zero-order hold between exchanges; HWIL
swaps only the transport. PR 2 ships the header and its compile checks only; transports are M4.

## Files to create

Build system
- `CMakeLists.txt` — `project(ASTRA CXX)`, C++23 no extensions, options `ASTRA_STATIC_PLUGINS` (OFF),
  `ASTRA_SANITIZERS` (string, "" or "address,undefined"), `ASTRA_BUILD_TESTS` (ON); warning set with
  `-Werror`; `find_package(Eigen3 3.4 REQUIRED)`; `include(cmake/AstraPlugin.cmake)`; `add_subdirectory(core)`;
  `astra_discover_plugins(plugins)`, `astra_discover_plugins(aircraft)` (globs are empty until M1, that is
  fine); `add_subdirectory(tests)`; `astra_finalize_plugins()` last.
- `CMakePresets.json` — hidden `base` (Ninja, `clang-18`/`clang++-18`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`,
  `binaryDir build/${presetName}`), configure presets `debug` (Debug), `release` (Release),
  `asan` (Debug + `ASTRA_SANITIZERS=address,undefined`), `static-plugins` (asan + `ASTRA_STATIC_PLUGINS=ON`);
  matching build and test presets.
- `cmake/AstraPlugin.cmake` — `astra_add_plugin(SOURCES ... [NO_LOAD_ALL])`, `astra_discover_plugins(<root>)`,
  `astra_finalize_plugins()`. Details under "Design".
- `cmake/check_undefined_symbol.cmake` — `-P` script: `nm -D --undefined-only <lib>`, assert a symbol is
  present or absent. Used by the "static build has no dlopen" test and its shared-mode inverse.
- `justfile` — `set shell := bash -euo pipefail`; `export PYTHONPATH := ""` (lesson in `docs/lessons.md`);
  recipes `configure preset`, `build preset="asan"`, `test preset="asan"` (build then `ctest --output-on-failure`),
  `link-db` (symlink `build/asan/compile_commands.json` at the repo root), `tidy` (`run-clang-tidy-18 -p build/asan`),
  `format` / `format-check` (`clang-format-18 --dry-run --Werror` over `core icd tests plugins aircraft`),
  `hooks-test` (`.claude/hooks/tests/run.sh`), `clean`, and `check: (test "debug") (test "release")
  (test "static-plugins") (test "asan") link-db tidy format-check hooks-test`. `default` is `check`.
- `.clang-format` — `BasedOnStyle: Google`, `ColumnLimit: 100` (docs in this repo run ~110 wide; 80 is the
  one Google default I propose to relax; say so if you want 80).
- `.clang-tidy` — `Checks: bugprone-*,performance-*,modernize-*,readability-*,-readability-identifier-length,
  -modernize-use-trailing-return-type` (the last one mandates `auto f() -> T`, which contradicts Google style;
  flagging as the one deviation from the CLAUDE.md list), `WarningsAsErrors: '*'`,
  `HeaderFilterRegex` limited to `core|plugins|aircraft|tests` so Eigen, gtest, and the C-style `icd/` names
  are not linted; `readability-identifier-naming` options encoding CLAUDE.md style (CamelCase types,
  snake_case functions/variables, trailing-underscore members, `k` constants).
- `tests/.clang-tidy` — `InheritParentConfig: true`, minus `readability-magic-numbers` and
  `readability-function-cognitive-complexity` (gtest macros and literal sizes like 200/64 are the test).

Core (`core/`)
- `core/CMakeLists.txt` — INTERFACE target `astra_core_headers` (include dirs, generated include dir,
  `Eigen3::Eigen` as SYSTEM, `cxx_std_23`, `ASTRA_STATIC_PLUGINS` define when ON); SHARED `astra_core`
  linking it PUBLIC and `${CMAKE_DL_LIBS}` PRIVATE in shared mode; alias `astra::core`, `astra::core_headers`.
- `core/include/astra/core_api.hpp.in` → generated `build/<preset>/generated/include/astra/core_api.hpp`:
  `kCoreApiVersion = 1` and `kBuiltWith = "<compiler id> <version> c++23 <build type> sanitizers=<...> static=<ON|OFF>"`.
- `core/include/astra/errors.hpp` — `astra::Error : std::runtime_error`; `PluginError` (holds `plugin_path()`);
  leaves `PluginNotFoundError`, `PluginLoadError` (dlopen/dlsym/null descriptor), `PluginApiVersionError`
  (expected/actual), `PluginToolchainError` (expected/actual strings). `what()` names the plugin path.
  Config-path field arrives with `Config` in a later milestone; noted in the header comment.
- `core/include/astra/interfaces/model.hpp` + `core/src/interfaces/model.cpp` — `StepContext {t, dt, frame}`
  and `Model` exactly per §7 (`init(const Config&, SignalRegistry&)`, `reset`, `step`, `name`), with
  `Config`/`SignalRegistry` forward-declared. The destructor is defined out of line in core so `Model`'s
  vtable and typeinfo are emitted once in `libastra_core.so` (hidden-visibility plugins otherwise get their
  own copy, which breaks `dynamic_cast` and UBSan's vptr check across the `.so` boundary).
- `core/include/astra/plugin/descriptor.hpp` — `PluginDescriptor` exactly per §4 and the
  `ASTRA_PLUGIN_EXPORT` macro (`extern "C"` + default visibility) the one exported symbol needs under
  `-fvisibility=hidden`.
- `core/include/astra/plugin/static_registry.hpp` — `StaticPluginEntry {path, entry}` and
  `static_plugin_table()`; declared only when `ASTRA_STATIC_PLUGINS`.
- `core/include/astra/plugin/loader.hpp` + `core/src/plugin/loader.cpp` — `PluginLoader(plugin_dir)`,
  `LoadedPlugin load(std::string_view plugin_path)` returning `{descriptor, model}`, static
  `library_name(path)` (`lib` + `astra_` + path with `/`→`_` + `.so`), static `is_static()`. Validates the
  path (segments of `[A-Za-z0-9_]`, no `..`, not absolute); shared mode: exists check → `dlopen(RTLD_NOW|RTLD_LOCAL)`
  → `dlsym("astra_plugin")` → descriptor checks; static mode: table lookup, no `<dlfcn.h>` in the TU at all.
  Handles are never `dlclose`d (a model may outlive the loader; the process is the unit), documented.
- `core/src/core_api.cpp` — `core_api_version()` / `built_with()` returning core's compiled values, so the
  loader compares against what `libastra_core.so` was built with, not what the caller was.
- `core/include/astra/kernel/state.hpp` — `StateVector = Eigen::Vector<double, 13>` and the ADR-0003 index
  constants (`kPosition=0, kVelocity=3, kQuaternion=6, kAngularRate=10, kSize=13`). Types only, no math.
  This is the one physics-adjacent file; it exists so the Eigen-as-SYSTEM-include rule under clang-18
  C++23 `-Werror` plus clang-tidy is proven now under all four presets, not discovered in M1.

ICD
- `icd/astra_icd.h` — §10 verbatim (include guard, `<stdint.h>` only, both structs, magic, version, size
  asserts) plus alignment asserts. The static asserts go through a two-line shim so the header is also valid
  C++ without extension warnings: `_Static_assert` in C, `static_assert` in C++.

Tests (`tests/`)
- `tests/CMakeLists.txt` — `find_package(GTest REQUIRED)`; `astra_discover_plugins(tests/plugins)`;
  `add_subdirectory(unit)`; the ICD C11 tests; the nm symbol tests.
- `tests/plugins/dummy/{CMakeLists.txt,dummy.cpp}` — `astra_add_plugin(SOURCES dummy.cpp)`; `DummyModel`
  (counts init/reset/step, `name()=="dummy"`), descriptor `{"test","dummy",kCoreApiVersion,kBuiltWith,create}`.
- `tests/plugins/bad_version/{CMakeLists.txt,bad_version.cpp}` — same, `core_api_version = kCoreApiVersion + 1`,
  `NO_LOAD_ALL`.
- `tests/plugins/bad_toolchain/{CMakeLists.txt,bad_toolchain.cpp}` — same, `built_with = "stale toolchain"`,
  `NO_LOAD_ALL`.
- `tests/unit/CMakeLists.txt` — one gtest binary `astra_unit_tests` (links `astra::core`, `GTest::gtest_main`,
  compile definition `ASTRA_TEST_PLUGIN_DIR`), `gtest_discover_tests` so `ctest -R <name>` works.
- `tests/unit/plugin/loader_test.cpp` — loads dummy by path and steps it; bad version → `PluginApiVersionError`
  whose `plugin_path()` and `what()` name `tests/plugins/bad_version`; bad toolchain → `PluginToolchainError`;
  unknown path → `PluginNotFoundError`; invalid paths (`../x`, `/abs`, `a//b`, empty) rejected;
  `library_name` mapping matches the ADR-0009 example; `is_static()` matches the build. Same file runs under
  both shared and static presets.
- `tests/unit/plugin/load_all_test.cpp` — iterates the generated `astra/all_plugins.hpp` list (every plugin
  registered without `NO_LOAD_ALL`), loads each, checks descriptor fields and version. This is the §4
  "loads every plugin once" test the helper registers for.
- `tests/unit/icd/icd_test.cpp` — sizes, alignment, and `offsetof` of every field as `static_assert`s under
  clang-18 C++23, plus one runtime `EXPECT` so it shows in ctest.
- `tests/icd/icd_c11.c` — same asserts in C11 (`<stddef.h>` for `offsetof`); registered twice as ctest
  commands `gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only` and `clang-18 -std=c11 ...`.
- `tests/unit/kernel/state_test.cpp` — the index constants partition 13 and the vector is fixed-size.
- ctest `core_has_no_dlopen` (static preset: `dlopen` absent from `libastra_core.so` undefined symbols) and
  `core_uses_dlopen` (shared presets: present), so the check is shown to be able to fire.

Hooks and CI
- `.claude/hooks/tests/run.sh` — runs every `*.cases` file against `../<name>.sh`; splits each line at the
  first tab into expected exit and JSON; substitutes `$ROOT_ON_MAIN` (a temp `git init -b main` repo with one
  empty commit) before `$ROOT` (a temp project root with `.claude/` and no `touch-goldens` marker); sets
  `CLAUDE_PROJECT_DIR` to that temp root and `ASTRA_TOUCH_GOLDENS=0` so the override can't leak in; prints
  PASS/FAIL per case with the hook's stderr on failure; exits 1 on any mismatch. No new cases files.
- `.github/workflows/check.yml` — `on: pull_request` (plus `push` to `main` and `workflow_dispatch`),
  `jobs.check` on `ubuntu-24.04`, steps: checkout, `sysctl vm.mmap_rnd_bits=28` (ASan on 6.x runner kernels),
  apt `clang-18 clang-tidy-18 clang-format-18 cmake ninja-build just libeigen3-dev libgtest-dev libgmock-dev`,
  `just check`. Concurrency group cancels superseded runs. No cache.

Files to edit
- `CLAUDE.md` — "Status" line and the Commands block: replace "Until the justfile exists" with the real
  recipes (`just check`, `just test <preset>`, `ctest -R`, presets list). Stays under 100 lines (now 87).
- `README.md` — status line and a two-line "Build" pointer to `just check`; the "Starting M0" section
  becomes "M0 progress" pointing at PLAN.md.
- `PLAN.md` — tick PR 2.
- `STATE.md` — rewritten at wrap: in flight (PR 2 awaiting merge), last verified result (the acceptance
  commands that ran and exited 0), next step (`claude --worktree m0-python`, PR 3 scope), tried/failed.
- `docs/lessons.md` — append only if something bites during the build (candidates I expect: the
  `Model` key-function rule, `mmap_rnd_bits` on CI).

Not in this PR: `plugins/`, `aircraft/`, `embedded/`, kernel classes (`Simulation`, `Scheduler`,
`VirtualClock`), `Config`, `SignalRegistry`, transports, Python, `.clangd`, skills, ruleset change.

## Design decisions worth reading before approving

1. **Static-plugins build with no second code path.** In static mode `astra_add_plugin` builds the plugin as
   an OBJECT library with the private compile definition `astra_plugin=astra_plugin_<path_with_underscores>`,
   so the plugin source still says `astra_plugin()` exactly as §4 shows. `astra_finalize_plugins()` writes
   `static_plugins.cpp` (declares each renamed entry, defines `static_plugin_table()`), adds it and every
   plugin object to `astra_core`, and the loader's `load()` does a table lookup. `libastra_core.so` in that
   preset has no undefined `dlopen`/`dlsym`, which ctest checks with `nm`.
2. **`built_with` string** is configure-time text baked into a generated header included by core and every
   plugin. A stale `.so` from an older configure keeps its old string and is rejected, which is the case the
   doc says the check exists for.
3. **Warnings**: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast
   -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Werror`
   on every preset. Eigen and gtest are SYSTEM includes. No `-ffast-math` anywhere.
4. **Sanitizers**: `-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer`, applied to
   core, plugins, and tests alike in the asan and static-plugins presets. If UBSan's vptr check gives a
   false positive across the plugin boundary despite the key-function fix, the fallback is
   `-fno-sanitize=vptr` with a lessons entry, not `-fsanitize-recover`.
5. **Plugin discovery** adds every found `CMakeLists.txt` with `add_subdirectory` directly; a parent that
   also adds its children would fail configure, which is the intended check. The same function discovers
   `tests/plugins`, so the discovery path is exercised in this PR even though `plugins/` and `aircraft/`
   don't exist yet.

## Order of work

1. Build files (CMakeLists, presets, justfile, format/tidy configs), empty `astra_core` builds under all
   four presets, `just format-check` and `just tidy` pass.
2. `icd/astra_icd.h` + C11 and C++23 tests.
3. `Model`, `PluginDescriptor`, errors, loader, dummy/bad plugins, loader tests; green under `asan`.
4. Static mode: rename definition, generated table, nm tests; green under `static-plugins`.
5. `.claude/hooks/tests/run.sh`; `just hooks-test` passes all 26 existing cases.
6. `.github/workflows/check.yml`.
7. CLAUDE.md, README.md; commit (message in a scratchpad file, `git commit -F`); push `worktree-m0-build`;
   `gh pr create` with the acceptance table; watch the `check` job; fix if red.
8. Wrap: PLAN.md tick, STATE.md rewrite, lessons if any, one-line opener for PR 3.

## Verification

- `just check` exits 0 locally (builds and tests debug, release, asan, static-plugins; tidy; format; hooks).
- `ctest --preset asan -R loader` shows the dummy loaded and the bad-version rejection;
  `ctest --preset static-plugins -R 'loader|no_dlopen'` shows the same tests plus the absent-`dlopen` check.
- `ctest --preset asan -R icd` shows the gcc C11, clang C11, and C++23 assert tests passing.
- `just hooks-test` prints 26 PASS lines and exits 0; corrupting one expected code makes it exit 1.
- GitHub Actions job `check` is green on the PR.
