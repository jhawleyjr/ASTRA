"""The F-16's generated C++ is a fresh run of its scripts on P1 (M7 PR 6b, Q14).

docs/plans/m7-pr6b-f16-aircraft.md: aircraft/f16/tools/generate_f16_tables.py emits the aero and
thrust tables, and aircraft/f16/tests/extract_f16_checkdata.py, a separate reader, emits the
checkData fixture the plugins' tests compare against. Each case runs one script into tmp_path with
its default --refs (the main checkout's docs/refs, found through `git rev-parse --git-common-dir`)
and compares every output with the committed file byte for byte. A second pair of cases shows that
each script refuses a source whose SHA-256 is not the inventory's (docs/plans/m7-f16-inventory.md
section 1.1).

P1 is gitignored, so these cases skip, naming the missing file, when the main checkout's docs/refs
lacks it (CI); they never skip for any other reason.

The scripts run as subprocesses with `-I -B`, so nothing writes `__pycache__` beside them, into a
tree that `just new-aircraft` or the copy tests might carry. Every case snapshots aircraft/f16/tools
and aircraft/f16/tests, ignored files included, and asserts nothing appeared or changed (lessons
2026-10-01, 2026-10-07).
"""

from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
from collections.abc import Iterator
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
F16 = REPO_ROOT / "aircraft" / "f16"
GENERATOR = F16 / "tools" / "generate_f16_tables.py"
READER = F16 / "tests" / "extract_f16_checkdata.py"
SOURCES = ("F16_aero.dml", "F16_prop.dml", "F16_inertia.dml")

# Each script's outputs: the name it writes under --out, and the committed file it must equal.
GENERATOR_OUTPUTS = {
    "aero_tables.hpp": F16 / "physical" / "aero" / "aero_tables.hpp",
    "engine_tables.hpp": F16 / "hardware" / "propulsion" / "engine" / "engine_tables.hpp",
}
READER_OUTPUTS = {"f16_checkdata.hpp": F16 / "tests" / "f16_checkdata.hpp"}


def _f16_package() -> Path:
    common = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "rev-parse", "--git-common-dir"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    common_path = Path(common)
    if not common_path.is_absolute():
        common_path = (REPO_ROOT / common_path).resolve()
    return common_path.parent / "docs" / "refs" / "Atmospheric_models" / "F16_package"


@pytest.fixture
def package() -> Path:
    found = _f16_package()
    for name in SOURCES:
        path = found / "F16_S119_source" / name
        if not path.is_file():
            pytest.skip(f"P1 is not in the main checkout's docs/refs: {path} does not exist")
    return found


def _snapshot() -> dict[str, str]:
    """Every file and directory under the scripts' two directories, ignored ones included."""
    entries: dict[str, str] = {}
    for root in (F16 / "tools", F16 / "tests"):
        for path in sorted(root.rglob("*")):
            key = path.relative_to(REPO_ROOT).as_posix()
            entries[key] = (
                hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else "<dir>"
            )
    return entries


@pytest.fixture(autouse=True)
def writes_nothing_beside_the_scripts() -> Iterator[None]:
    before = _snapshot()
    yield
    assert _snapshot() == before, "a script run wrote into aircraft/f16/tools or aircraft/f16/tests"


def _run(script: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-I", "-B", str(script), *args],
        capture_output=True,
        text=True,
        timeout=120,
        check=False,
    )


@pytest.mark.parametrize(
    ("script", "outputs"),
    [
        pytest.param(GENERATOR, GENERATOR_OUTPUTS, id="tables"),
        pytest.param(READER, READER_OUTPUTS, id="checkdata"),
    ],
)
def test_committed_output_equals_a_fresh_run(
    package: Path, tmp_path: Path, script: Path, outputs: dict[str, Path]
) -> None:
    del package  # the fixture only gates on P1; the script finds it by itself (default --refs)
    result = _run(script, "--out", str(tmp_path))
    assert result.returncode == 0, result.stdout + result.stderr
    assert sorted(p.name for p in tmp_path.iterdir()) == sorted(outputs)
    for name, committed in outputs.items():
        fresh = (tmp_path / name).read_bytes()
        assert fresh == committed.read_bytes(), f"{committed} differs from a fresh run"


@pytest.mark.parametrize("script", [GENERATOR, READER], ids=["tables", "checkdata"])
def test_a_changed_source_is_refused(package: Path, tmp_path: Path, script: Path) -> None:
    refs = tmp_path / "refs"
    source = refs / "F16_S119_source"
    source.mkdir(parents=True)
    for name in SOURCES:
        shutil.copyfile(package / "F16_S119_source" / name, source / name)
    aero = source / "F16_aero.dml"
    data = bytearray(aero.read_bytes())
    data[len(data) // 2] ^= 0x01  # one bit of one byte: still ASCII, never the same file
    aero.write_bytes(bytes(data))
    out = tmp_path / "out"

    result = _run(script, "--refs", str(refs), "--out", str(out))

    assert result.returncode == 1, result.stdout + result.stderr
    assert "SHA-256" in result.stderr
    assert not out.exists() or not any(out.iterdir()), "a refused run wrote output"
