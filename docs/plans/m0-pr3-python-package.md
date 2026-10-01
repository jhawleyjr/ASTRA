# M0 PR 3 — Python package (`astra._core` via nanobind 3 + scikit-build-core)

Recovered from `~/.claude/plans/task-m0-pr-3-agile-crane.md` on 2026-09-30. Approval: the file records none; the work merged as #6. Never committed: it predates `docs/plans/`, whose first plan was committed on 2026-09-19 (#31).

## Context

PLAN.md M0 PR 3: the Python side of the bootstrap. `docs/architecture.md` §12 says `astra._core` is a nanobind 3
module built with scikit-build-core, with `.pyi` stubs from `nanobind_add_stub` so pyright and the LSP see real
types; §14 says Python is `ruff` + `pyright` strict on `python/astra/`; `docs/process/workflow.md` says `just check`
grows pytest and `pyrightconfig.json` points at the venv. Gate on current HEAD passed in this session
(`just check` exit 0: 4 presets × 16 ctest cases, tidy, format, hooks 25/25).

What the module binds in M0 is what exists in the core today: the ADR-0003 state layout (`core/include/astra/
kernel/state.hpp`), the ADR-0009 loader (`loader.hpp`, `descriptor.hpp`, `errors.hpp`, `model.hpp`), and the
core API/toolchain strings. ADR-0002 binds by omission: no unit suffixes in any bound name; the state round-trip is
plain `double`s in SI.

**Environment fact that shapes the design:** the venv lives at `/home/joe/repos/ASTRA/.venv` (main checkout).
Every task session is a fresh worktree with no `.venv`, and CI has none, so `just check` creates the venv itself
(idempotent `just venv`; gate-only deps ≈ 90 MB from the pip cache) or it cannot be the gate in either place. A
shared venv was rejected: one editable install can only point at one source tree, so two worktrees would fight.

**Already verified in this session with a scratch build outside the repo** (same headers, linked against
`build/release/lib/libastra_core.so`): the whole surface below compiles under the repo's full `-Werror` flag set
once nanobind's `NB_SUPPRESS_WARNINGS` marks Python and nanobind headers SYSTEM (without it, `Python.h` trips
`-Wold-style-cast`); the repo's clang-tidy config is silent on the `NB_MODULE` TU; `nanobind_add_stub` emits
`Annotated[NDArray[numpy.float64], dict(shape=(13), order='C')]` for the Eigen 13-vector; every loader behavior and
the exception hierarchy behave from Python; `LD_DEBUG` shows exactly one `libastra_core.so`; pyright strict on the
stub is clean after one `nb::sig` override (`std::filesystem::path` otherwise stubs as bare `os.PathLike`).

## Approach (five lines)

1. `pyproject.toml`: scikit-build-core backend, `build-dir = build/python`, Release, clang++-18, `ASTRA_BUILD_PYTHON=ON`;
   `just python` = `pip install --no-build-isolation --no-deps -e .` into `.venv`, which `just venv` creates and fills
   from a pinned `[dependency-groups] dev`.
2. `python/CMakeLists.txt` (added by the top level only when `SKBUILD` or `ASTRA_BUILD_PYTHON`) builds `_core` from
   `python/bindings/_core.cpp` against `astra::core` (inherits `-Werror` flags), sets `INSTALL_RPATH` to the build's
   `lib/`, installs `_core` into `astra/`, and writes the stub to `python/astra/_core.pyi` (gitignored, generated at
   build).
3. Bindings: state layout constants + `zero_state()/identity_state()` (Eigen ↔ numpy), `PluginLoader`/`LoadedPlugin`/
   `PluginDescriptor`/`Model`/`StepContext`, the `Error` → `PluginError` → four leaf exception hierarchy,
   `core_api_version()`, `built_with()`, `default_plugin_dir()`, `__version__`.
4. Tooling: `pyrightconfig.json` (venv `.venv`, strict on `python/astra`, tests at standard), `[tool.ruff]`,
   `[tool.pytest.ini_options]`, `.clangd`; `just link-db` also links `python/compile_commands.json`; `just tidy` adds a
   pass over `build/python` restricted to `python/bindings/`.
5. `just check` grows `python ruff pyright pytest`; CI apt adds `python3-venv python3-dev`; pytest mirrors the
   GoogleTest cases in `tests/python/`.

Presets stay C++-only: a sanitized extension cannot be imported by a plain interpreter without `LD_PRELOAD`, and
nanobind's stub generator only detects sanitizer flags one link level deep (ours sit on `astra_build_flags` two
levels down), so the module is built once, unsanitized, by the pip step.

## Acceptance

```
just check                                   # exit 0
.venv/bin/python -c "import astra._core"     # exit 0, from the worktree root
```

## Files

New:
- `pyproject.toml` — `[build-system]` scikit-build-core + nanobind; `[project]` name `astra`, version `0.1.0`
  (must equal `project(ASTRA VERSION ...)` in `CMakeLists.txt`; a pytest checks it), `requires-python >=3.12`,
  `dependencies = ["numpy>=2"]`; `[dependency-groups] dev` pinned to what the venv has today (nanobind==3.0.1,
  scikit-build-core==1.0.3, pytest==9.1.1, ruff==0.16.7, pyright==1.1.414, numpy>=2; the analysis libraries
  scipy/h5py/pandas/pyarrow/pyyaml stay out of the gate group until a milestone uses them); `[tool.scikit-build]`
  `build-dir = "build/python"`, `cmake.build-type = "Release"`, `cmake.define = { CMAKE_CXX_COMPILER = "clang++-18",
  ASTRA_BUILD_PYTHON = "ON" }`, `wheel.packages = ["python/astra"]`, `editable.mode = "redirect"`,
  `editable.rebuild = false`; `[tool.ruff]` line-length 100, py312, `src = ["python", "tests/python"]`,
  `extend-exclude = ["python/astra/_core.pyi"]` (generated), lint select `E W F I N UP B SIM PT RUF`;
  `[tool.pytest.ini_options]` `testpaths = ["tests/python"]`, `filterwarnings = ["error"]`, `xfail_strict = true`.
- `python/CMakeLists.txt` — `find_package(Python 3.12 COMPONENTS Interpreter Development.Module REQUIRED)`,
  `nanobind_ROOT` from `${Python_EXECUTABLE} -m nanobind --cmake_dir`, `find_package(nanobind CONFIG REQUIRED)`,
  `nanobind_add_module(_core NB_STATIC NB_SUPPRESS_WARNINGS bindings/_core.cpp)`, `target_link_libraries(_core PRIVATE
  astra::core)`, compile defs `ASTRA_VERSION="${PROJECT_VERSION}"` and `ASTRA_PLUGIN_DIR="${ASTRA_PLUGIN_OUTPUT_DIR}"`
  (same pattern as `ASTRA_TEST_PLUGIN_DIR` in `tests/unit/CMakeLists.txt`), `INSTALL_RPATH "${CMAKE_BINARY_DIR}/lib"`,
  `install(TARGETS _core LIBRARY DESTINATION astra)`, `nanobind_add_stub(astra_core_stub MODULE _core PYTHON_PATH
  $<TARGET_FILE_DIR:_core> DEPENDS _core OUTPUT "${ASTRA_SOURCE_DIR}/python/astra/_core.pyi")`.
- `python/bindings/_core.cpp` — the `NB_MODULE(_core, m)`; the scratch probe (`probe.cpp` in the scratchpad) is its
  first draft; includes `nanobind/eigen/dense.h`, `nanobind/stl/filesystem.h`, `nanobind/stl/string.h`,
  `nanobind/stl/string_view.h`.
- `python/astra/__init__.py` (docstring, `from astra._core import __version__`, `__all__`), `python/astra/py.typed`.
- `pyrightconfig.json` — `venvPath "."`, `venv ".venv"`, `pythonVersion "3.12"`, `include ["python/astra",
  "tests/python"]`, `extraPaths ["python"]`, `strict ["python/astra"]`, `reportMissingModuleSource "none"` with a
  comment: the compiled `_core` lives in the venv, its stub in the tree, so pyright cannot pair them.
- `.clangd` — short: comment on where the compile DBs come from, `Diagnostics.UnusedIncludes: Strict`,
  `Index.Background: Build`.
- `tests/python/test_state.py`, `tests/python/test_loader.py`, `tests/python/test_package.py`.

Modified:
- `CMakeLists.txt` — `option(ASTRA_BUILD_PYTHON "..." OFF)`; after `add_subdirectory(core)`:
  `if(SKBUILD OR ASTRA_BUILD_PYTHON) add_subdirectory(python) endif()`.
- `justfile` — recipes `venv` (create `.venv` if missing and upgrade its pip to ≥25.1 on creation only, since the
  bundled pip 24.0 lacks `--group`; then `pip install -q --group dev`, which is offline when satisfied), `python: venv`
  (editable install), `ruff: venv` (`ruff check .` + `ruff format --check .`), `pyright: python`, `pytest: python`;
  `check` becomes `... hooks-test python ruff pyright pytest`; `link-db` adds `ln -sfn
  ../build/python/compile_commands.json python/compile_commands.json`; `tidy` adds `run-clang-tidy-18 ... -p
  build/python '/python/bindings/'`; `format` adds `ruff format .`; `clean` also removes `python/astra/_core.pyi` and
  `python/compile_commands.json`.
- `.gitignore` — `python/astra/_core.pyi`, `python/astra/_core*.so`.
- `.github/workflows/check.yml` — apt adds `python3 python3-venv python3-dev`.
- `scripts/setup-ubuntu.sh` — the venv block becomes `just venv` plus one unpinned line for the analysis libraries.
- `CLAUDE.md` (90/100 lines) — Commands: `just venv`/`just python`, `pytest tests/python -k <name>` single-test line,
  and that `just check` now includes ruff, pyright, pytest; drop the "later milestones add pytest" clause.
- `PLAN.md`, `STATE.md`, `docs/lessons.md` at `/wrap`.

## The `_core` surface (M0)

```
__version__: str                        # PROJECT_VERSION; test compares to importlib.metadata.version("astra")
core_api_version() -> int               # astra::core_api_version()
built_with() -> str                     # astra::built_with()
default_plugin_dir() -> str             # ASTRA_PLUGIN_OUTPUT_DIR of the build that made this module
STATE_SIZE = 13; POSITION = 0; VELOCITY = 3; QUATERNION = 6; ANGULAR_RATE = 10
POSITION_SIZE = 3; VELOCITY_SIZE = 3; QUATERNION_SIZE = 4; ANGULAR_RATE_SIZE = 3
zero_state() -> NDArray[float64] (13,)  # StateVector::Zero() through nanobind/eigen/dense.h
identity_state() -> NDArray[float64]    # zero with q_nb = (1, 0, 0, 0), scalar-first (ADR-0001)
class StepContext(t: float, dt: float, frame: int)   # read/write fields
class Model: name() -> str; reset(); step(ctx)       # abstract; no init() until Config/SignalRegistry exist
class PluginDescriptor: category, name, core_api_version, built_with   (read-only)
class LoadedPlugin: descriptor (reference), model (reference_internal)
class PluginLoader(plugin_dir: str | os.PathLike[str]): load(path) -> LoadedPlugin; plugin_dir; static
      library_name, is_valid_plugin_path, is_static
Error(RuntimeError) > PluginError > {PluginNotFoundError, PluginLoadError, PluginApiVersionError, PluginToolchainError}
```

Exceptions: `nb::exception<T>` registered base-first (nanobind runs translators most-recent-first, verified in
`nanobind/src/error.cpp` and by the probe). The message already names the plugin (`errors.cpp`); attributes
(`plugin_path`, `expected`, `actual`) are not added in M0. Python-defined models (trampolines) are architecture §7
but not in this PR's task text; not built here.

## Verification

1. `just check` exits 0 in the worktree (creates `.venv`, builds `build/python`, stub lands at
   `python/astra/_core.pyi`, ruff/pyright/pytest green).
2. `.venv/bin/python -c "import astra._core"` exits 0.
3. pytest cases (each mirrors a GoogleTest in `tests/unit/`):
   - `test_state.py`: constants partition 13 as ADR-0003 says; `zero_state()` shape `(13,)`, dtype float64, all zero,
     C-contiguous; `identity_state()[QUATERNION] == 1` and the rest zero. No function takes a state in M0; the
     taking-side caster gets its first consumer with `Simulation.derivative` in M1 (ADR-0003).
   - `test_loader.py`: loads `tests/plugins/dummy` from `default_plugin_dir()`; descriptor fields; `model.name() ==
     "dummy"`; `reset()`, `step(StepContext(0.0, 1e-3, 0))`; two loads give distinct models; `bad_version` raises
     `PluginApiVersionError` whose message names the plugin and which is a `PluginError`, an `Error`, a `RuntimeError`;
     `bad_toolchain` → `PluginToolchainError`; unknown path → `PluginNotFoundError`; malformed paths (same seven as
     `loader_test.cpp`) → `is_valid_plugin_path` False and `load` raises `PluginNotFoundError`; `library_name`
     mapping; `is_static()` is False in the pip build; a model proxy outlives its `LoadedPlugin`; a negative
     `frame` is a `TypeError`.
   - `test_package.py`: `__version__ == importlib.metadata.version("astra")`; `core_api_version() == 1`;
     `"static-plugins=off" in built_with()`; `default_plugin_dir()` is an existing directory containing
     `libastra_tests_plugins_dummy.so`.
4. Negative checks shown to fire once each (not committed): a deliberate ruff violation fails `just ruff`; an
   un-annotated function in `python/astra/` fails `just pyright`; a wrong version in `pyproject.toml` fails
   `test_package.py`.
5. `LD_DEBUG=libs .venv/bin/python -c "import astra._core"` shows one `libastra_core.so` (from `build/python/lib`).
6. `just check` re-run is incremental (no rebuild of the module when nothing changed) and still exits 0.
7. `/code-review` subagent with the intent line and ADR-0002/0003/0009, correctness and ADR violations only; then
   commit, push, `gh pr create`; `/wrap`.

## Remaining risks

- **`filterwarnings = error` + numpy 2.x** may surface a DeprecationWarning on import; fix the cause or narrow the
  filter with a comment, never silence globally.
- **CI pyright** uses the runner's global node if present (pyright-python defaults to it), else downloads one via
  nodeenv; either way it is inside the 30-minute budget.
- **scikit-build-core editable redirect** must serve `python/astra/_core.pyi` from source and `_core.so` from
  site-packages; the acceptance import proves the runtime side, pyright the static side.
