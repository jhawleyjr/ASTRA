# The embedded-standalone dependency check reads dependencies, not paths

Risk: **Tier 1** (Joe, 2026-10-04): the gate's `embedded-standalone` check plus its test; no product code, no
interface or numerical effect. Base: `origin/main` `72877da`. Worktree/branch: `worktree-embedded-deps-check`.
Status: direction from Joe, 2026-10-04, in M6 PR 11's session ("the check is wrong, not the path").

## Problem

`just embedded-standalone` refused any configure whose `CMakeCache.txt` matched `eigen|yaml|gtest|python`
anywhere. The cache records the build and source directories, so a checkout whose path contains one of the words
fails the full gate with "configure pulled in a dependency beyond a C++17 compiler". M6 PR 11's worktree
(`.claude/worktrees/m6-pr11-yaml-route`) hit it: all six matches were that path.

## Change

- `scripts/embedded_deps_check.py <build_dir> --root <checkout>` fails, naming each hit, on any of these:
  - a cache entry whose **name** matches (`yaml-cpp_DIR`, `Eigen3_DIR`, `GTest_DIR`, `Python3_EXECUTABLE`: every
    `find_package` result is named after its package);
  - a `PATH`/`FILEPATH` entry whose **value** matches, with the checkout root removed (`find_library`, `find_path`
    or `find_program` results under any name; a dependency vendored inside the checkout still matches);
  - a generated **compile or link line** (`flags.make`, `link.txt`, `build.ninja`) that matches, with the
    checkout root removed.
- The `justfile` recipe calls it in place of the grep. The message and the rest of the recipe are unchanged.
- `docs/process/validation.md` row and a lessons entry.

## Acceptance

- `tests/python/test_embedded_deps_check.py` (`-k embedded_deps_check`):
  - a synthetic cache under a path holding all four words passes;
  - each dependency entry (`yaml-cpp_DIR`, `Eigen3_DIR`, `GTest_DIR`, `Python3_EXECUTABLE`, a `find_library`
    `FILEPATH`, a `find_path` `PATH`), a vendored path inside the checkout, `-lyaml-cpp` on a link line and
    `-isystem /usr/include/eigen3` on a compile line each fail;
  - a real `cmake -S embedded` configure (a copy with `cmake/` and `icd/`) under such a path passes, and the
    same copy with `find_package(yaml-cpp REQUIRED)` fails, naming `yaml-cpp_DIR`.
- `just embedded-standalone` passes in this worktree; the full gate passes; then M6 PR 11 merges `main` and
  reruns its gate in `m6-pr11-yaml-route`.
