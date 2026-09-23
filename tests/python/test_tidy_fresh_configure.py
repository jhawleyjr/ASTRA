"""`just check`'s `tidy` step on a fresh configure, before any build (M3 PR 10,
docs/plans/pr10-fresh-tidy.md).

tests/capture/capture_cruise_regression is EXCLUDE_FROM_ALL, so nothing in `just check` ever builds
it, yet its TU is in build/asan's compile database and `tidy` lints it. With C++ module scanning
on (CMake >= 3.28's default for C++20+ under Ninja) its entry carried an `@<object>.modmap`
argument that only a build of the target writes, so `run-clang-tidy-18` failed on it from every
fresh checkout.

The case configures the asan preset into tmp_path -- never the real build/asan, which an earlier
build or capture run may have primed -- and runs the `tidy` recipe's own first `run-clang-tidy-18`
line from the justfile, pointed at that directory and restricted to the harness TU. No target is
built.
"""

from __future__ import annotations

import json
import os
import re
import shlex
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HARNESS_TU = "tests/capture/capture_cruise_regression.cpp"


def _env() -> dict[str, str]:
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.pop("ASTRA_TOUCH_GOLDENS", None)
    return env


def _tidy_recipe_first_invocation() -> list[str]:
    """The `tidy` recipe's first `run-clang-tidy-18` command, split into argv as written."""
    lines = (REPO_ROOT / "justfile").read_text(encoding="utf-8").splitlines()
    start = next(i for i, line in enumerate(lines) if line.startswith("tidy:"))
    for line in lines[start + 1 :]:
        if not line.startswith((" ", "\t")):
            break
        if line.strip().startswith("run-clang-tidy-18"):
            return shlex.split(line.strip())
    raise AssertionError("justfile's tidy recipe has no run-clang-tidy-18 line")


def test_tidy_passes_on_the_exclude_from_all_harness_from_a_fresh_configure(tmp_path: Path) -> None:
    build = tmp_path / "asan"
    subprocess.run(
        ["cmake", "--preset", "asan", "-B", str(build)],
        cwd=REPO_ROOT,
        env=_env(),
        check=True,
        capture_output=True,
        text=True,
        timeout=300,
    )

    # run-clang-tidy-18 exits 0 when its file filter matches nothing, so the harness TU must be
    # present in the database for the exit status below to mean anything.
    database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    assert any(Path(entry["file"]) == REPO_ROOT / HARNESS_TU for entry in database)

    argv = _tidy_recipe_first_invocation()
    assert argv[argv.index("-p") + 1] == "build/asan"
    argv[argv.index("-p") + 1] = str(build)
    argv[argv.index("-j") + 1] = str(os.cpu_count() or 1)
    argv.append(re.escape(str(REPO_ROOT / HARNESS_TU)) + "$")

    tidy = subprocess.run(
        argv, cwd=REPO_ROOT, env=_env(), capture_output=True, text=True, timeout=600, check=False
    )
    assert tidy.returncode == 0, tidy.stdout + tidy.stderr
