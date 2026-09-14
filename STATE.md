# STATE (rewritten each session, never appended; ≤ 40 lines)

## In flight
M0 PR 2 on branch `worktree-m0-build`: CMake presets, `justfile`, core skeleton, plugin loader + test plugins,
`icd/astra_icd.h`, hook test runner, GitHub Actions `check`. Pushed; PR open; awaiting Joe's squash-merge.

## Last verified result
- `just check` exited 0 in this session: presets `debug`, `release`, `asan`, `static-plugins` each configure,
  build with clang-18 `-Werror`, and pass 16 ctest cases; `run-clang-tidy-18` clean with the repo config
  (verified applied via `--dump-config`); `clang-format --dry-run --Werror` clean; hook runner 25/25 cases.
- Loader: loads `tests/plugins/dummy` by path; `PluginApiVersionError` and `PluginToolchainError` name the
  plugin in `what()` and `plugin_path()`; unknown and malformed paths → `PluginNotFoundError`. Static preset
  runs the same tests and ctest `core_has_no_dlopen` proves `libastra_core.so` has no undefined `dlopen`
  (shared presets run the inverse, `core_uses_dlopen`).
- ICD: sizes 200/64, alignment 8, every field offset pinned under clang-18 C++23 and under `gcc -std=c11`
  and `clang-18 -std=c11` (`-Wpedantic -Werror`).
- Negative paths shown to fire: a corrupted hook case → runner exit 1; symbol check told the wrong
  expectation → exit 1; a wrong ICD size → gcc static-assert error.
- The dummy plugin's shared object exports exactly one symbol, `astra_plugin`.
- CI: workflow `check` (job `check`, `ubuntu-24.04`) passed on PR #2's first run, about two minutes.

## Next step
M0 PR 3 in a fresh session: `claude --worktree m0-python --effort xhigh --permission-mode plan`. Scope per
`PLAN.md`: `pyproject.toml` (scikit-build-core + nanobind 3), `astra._core` with `.pyi` stubs,
`pyrightconfig.json`, ruff, pytest scaffold, `.clangd`; `just check` grows ruff/pyright/pytest. The venv at
`.venv/` already has nanobind 3.0.1, scikit-build-core, pytest, ruff, pyright. `compile_commands.json` is
linked at the repo root by `just link-db` (asan preset).

## Tried / failed
- Nothing failed in the build. UBSan's vptr check across the plugin boundary did not fire; the out-of-line
  `Model::~Model()` (key function in core) was in place from the start, so the cause is not isolated.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
