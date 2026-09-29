"""`just new-aircraft`, then a clean build and test of the copy, with no edit outside it (§18).

The copy's own `tests/` directory (aircraft/_template/tests) opts into the plugin build order with
`astra_add_plugin_test()`, so nothing in cmake/AstraPlugin.cmake, the top-level CMakeLists.txt or
any other framework file names the new aircraft. Each case works in a throwaway copy of the tree:

1. copy the working tree's build inputs into tmp_path (uncommitted edits included, so a scratch
   mutation of aircraft/_template/tests/CMakeLists.txt or cmake/AstraPlugin.cmake is exercised);
2. `just new-aircraft gliderx` there;
3. configure a fresh build directory outside that tree, build only the copy's test target, and run
   the copy's tests under ctest.

Step 3's target-only build is what makes the shared case an ordering test: without the dependency
that astra_add_plugin_test() records, ninja builds the test executable and no plugin, and the tests'
own `PluginLoader::load` fails. The static case (debug plus ASTRA_STATIC_PLUGINS=ON, not the asan
static-plugins preset) shows that `_template` and the copy link and run in one libastra_core. It is
not an ODR-isolation test: the two aircraft are identical and their model classes sit in anonymous
namespaces. tests/unit/aero/aero_math_namespace_isolation_test.cpp covers that.

The second test removes the template's astra_add_plugin_test() call in a throwaway copy and only
configures it: astra_finalize_plugins() must refuse the unregistered target in both builds, naming
it and its directory, so the omission fails configure instead of a later targeted or clean build.

No git runs in the throwaway tree from this file. `GIT_*` is removed from every subprocess's
environment, as tests/python/test_tidy_fresh_configure.py does: core's build-info step runs
`git describe` in the source directory, and a hook-set GIT_DIR or GIT_INDEX_FILE would point it at
the real repository. GIT_CEILING_DIRECTORIES stops the lookup at tmp_path's parent for the same
reason.
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
AIRCRAFT = "gliderx"

# What a configure and build of the default targets reads. python/ (ASTRA_BUILD_PYTHON is off),
# docs/ and the Python tests are not inputs.
_COPIED = (
    "CMakeLists.txt",
    "CMakePresets.json",
    "justfile",
    "cmake",
    "core",
    "icd",
    "embedded",
    "plugins",
    "aircraft",
    "tests",
    "scripts/new-aircraft.sh",
)


def _env(ceiling: Path) -> dict[str, str]:
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.pop("ASTRA_TOUCH_GOLDENS", None)
    env["GIT_CEILING_DIRECTORIES"] = str(ceiling)
    return env


def _copy_sources(root: Path) -> None:
    for entry in _COPIED:
        source = REPO_ROOT / entry
        target = root / entry
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.is_dir():
            shutil.copytree(source, target, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        else:
            shutil.copy2(source, target)


def _snapshot(root: Path) -> dict[str, str]:
    """Relative path to content hash, for every file under root."""
    return {
        path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def _run(argv: list[str], cwd: Path, env: dict[str, str], timeout: int) -> str:
    result = subprocess.run(
        argv, cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout, check=False
    )
    output = result.stdout + result.stderr
    assert result.returncode == 0, f"{' '.join(argv)} exited {result.returncode}:\n{output[-6000:]}"
    return output


def _template_test_count() -> int:
    """Every gtest the template's tests directory defines, so the copy must run all of them."""
    sources = (REPO_ROOT / "aircraft" / "_template" / "tests").glob("*.cpp")
    return sum(len(re.findall(r"^TEST\(", p.read_text(encoding="utf-8"), re.M)) for p in sources)


_PLUGIN_MODES = [
    pytest.param([], id="shared-plugins"),
    pytest.param(["-DASTRA_STATIC_PLUGINS=ON"], id="static-plugins"),
]


@pytest.mark.parametrize("extra_defines", _PLUGIN_MODES)
def test_copied_aircraft_builds_from_clean_and_runs_its_tests(
    tmp_path: Path, extra_defines: list[str]
) -> None:
    root = tmp_path / "repo"
    root.mkdir()
    build = tmp_path / "build"
    env = _env(tmp_path)
    jobs = str(os.cpu_count() or 1)

    _copy_sources(root)
    before = _snapshot(root)

    _run(["just", "new-aircraft", AIRCRAFT], root, env, timeout=60)
    after_copy = _snapshot(root)
    added = sorted(set(after_copy) - set(before))
    assert set(before) <= set(after_copy)
    assert all(after_copy[p] == before[p] for p in before), "new-aircraft edited an existing file"
    assert added, "new-aircraft added nothing"
    assert all(p.startswith(f"aircraft/{AIRCRAFT}/") for p in added), added
    # The copy carries the template's tests directory, with its own CMakeLists.txt.
    assert f"aircraft/{AIRCRAFT}/tests/CMakeLists.txt" in added

    # cmake --preset debug is the repo's own preset; -B keeps the build out of the source tree.
    _run(["cmake", "--preset", "debug", "-B", str(build), *extra_defines], root, env, timeout=300)
    target = f"astra_aircraft_{AIRCRAFT}_tests"
    _run(["cmake", "--build", str(build), "--target", target, "--parallel", jobs], root, env, 900)

    if not extra_defines:
        # The shared build's plugin .so files exist only because the target depends on them.
        plugin = build / "plugins" / f"libastra_aircraft_{AIRCRAFT}_physical_mass.so"
        assert plugin.is_file(), f"{plugin.name} was not built with {target}"

    expected = _template_test_count()
    assert expected > 0
    output = _run(
        [
            "ctest",
            "--test-dir",
            str(build),
            "-R",
            f"^aircraft_{AIRCRAFT}_tests\\.",
            "--no-tests=error",
        ],
        root,
        env,
        timeout=300,
    )
    assert f"100% tests passed, 0 tests failed out of {expected}" in output, output

    # Configure, build and test wrote nothing into the source tree and changed nothing outside the
    # copy (one snapshot: same paths, same hashes).
    assert _snapshot(root) == after_copy


@pytest.mark.parametrize("extra_defines", _PLUGIN_MODES)
def test_unregistered_plugin_test_fails_configure(tmp_path: Path, extra_defines: list[str]) -> None:
    root = tmp_path / "repo"
    root.mkdir()
    build = tmp_path / "build"
    env = _env(tmp_path)

    _copy_sources(root)
    cmakelists = root / "aircraft" / "_template" / "tests" / "CMakeLists.txt"
    lines = cmakelists.read_text(encoding="utf-8").splitlines(keepends=True)
    kept = [line for line in lines if not line.startswith("astra_add_plugin_test(")]
    # Exactly the one call is gone; the header comment's mention of it does not start a line.
    assert len(lines) - len(kept) == 1
    cmakelists.write_text("".join(kept), encoding="utf-8")

    result = subprocess.run(
        ["cmake", "--preset", "debug", "-B", str(build), *extra_defines],
        cwd=root,
        env=env,
        capture_output=True,
        text=True,
        timeout=300,
        check=False,
    )
    output = result.stdout + result.stderr
    assert result.returncode != 0, output[-6000:]
    # The check's own offender line, so the failure is this refusal and not another configure error.
    assert "astra_aircraft__template_tests (aircraft/_template/tests)" in output, output[-6000:]
