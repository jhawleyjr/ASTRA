# STATE (rewritten each session, never appended; ≤ 40 lines)

## In flight
M0 PR 3 on branch `worktree-m0-python`: `pyproject.toml` (scikit-build-core + nanobind 3), `astra._core`
(`python/bindings/_core.cpp`) with the generated stub, `pyrightconfig.json`, ruff, pytest scaffold
(`tests/python/`), `.clangd`; `just check` grew `venv`, `python`, `ruff`, `pyright`, `pytest`. Rebased onto
`main` after PR #7; PR #6 open. Awaiting Joe: `/codex:review --base origin/main --wait` (user-invoked only; the
built-in `/code-review` subagent ran before that rule landed, its findings are fixed), then squash-merge.

## Last verified result
- `just check` exited 0 in this session on the branch rebased onto current `main`: 4 presets × 16 ctest cases,
  `run-clang-tidy-18` clean over `build/asan` and over the binding TU from `build/python`, clang-format clean,
  hooks 25/25, `ruff check` + `ruff format --check` clean, `pyright` strict 0 errors, pytest 26 passed.
- Acceptance: `.venv/bin/python -c "import astra._core"` exited 0 from the worktree root (the worktree had no
  `.venv`; `just venv` created it from the pinned dev group and `just python` installed the module editable).
- `LD_DEBUG=libs` shows exactly one `libastra_core.so` in the process (`build/python/lib`), and the dummy plugin
  dlopened from `build/python/plugins` resolves against it by SONAME.
- Negative paths shown to fire: an unused import → `just ruff` exit 1 (F401); an un-annotated function in
  `python/astra/` → `just pyright` exit 1; CMake version bumped without reinstalling → `test_package.py` fails.
- clang-tidy over the binding TU is live: unnamed `nb::exception` temporaries failed the gate until named.
- Review fixes verified: a stale editable install (build/python reconfigured as Debug, module not reinstalled)
  fails `import astra._core` with an ImportError naming both toolchains; a deleted `_core.pyi` regenerates.
- CI: workflow `check` passed on PR #6's first run (about three and a half minutes), venv built on the runner.

## Next step
M0 PR 4 in a fresh session: `claude --worktree m0-hooks --effort xhigh --permission-mode plan`. Scope per
`PLAN.md`: Stop hook running the fast check (build + unit tests), `/wrap` `/retro` skills, and `check` added as a
required status check to the `main` ruleset (`.github/rulesets/main.json`, then `gh api -X PUT`). Precondition on
Joe's machine: none beyond `gh auth`. After PR 4, M0 closes; M1 (rigid body) starts at `xhigh`.

## Tried / failed
- Tried building `astra._core` under the sanitizer presets: not viable. A sanitized extension needs
  `LD_PRELOAD` in the interpreter, and nanobind's stub generator only detects `-fsanitize` one link level deep
  (ours sits on `astra_build_flags`). The module is built once, unsanitized, by scikit-build-core.
- Tried exposing `__version__` from the module: nanobind's stub generator never emits that name, so pyright
  strict failed. The module exposes `VERSION`; `astra/__init__.py` re-exports it as `__version__`.
- The pyright LSP plugin did not pick up the new `pyrightconfig.json` mid-session (the CLI passed); recheck next start.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
