"""tests/capture/capture_cruise_regression.sh's own refusals (M3 PR 8, D20,
docs/plans/pr8-cruise-regression.md): the guard, not the happy path. Each case runs a byte-identical
copy of the script at its real relative path inside a throwaway `git init` tree, so the script's
own ROOT (resolved from its location) is that tree -- never this checkout, never the main checkout's
.claude/touch-goldens marker -- and ASTRA_TOUCH_GOLDENS is stripped from the environment, so a
session Joe has armed still sees the unarmed cases as unarmed. The throwaway tree has no
CMakePresets.json either, so even a guard regression could not start a real capture.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_REL = Path("tests/capture/capture_cruise_regression.sh")


def _git(tree: Path, *args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(tree), *args], check=True, capture_output=True, text=True
    ).stdout


def _env(**overrides: str) -> dict[str, str]:
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.pop("ASTRA_TOUCH_GOLDENS", None)
    env.update(overrides)
    return env


@pytest.fixture
def tree(tmp_path: Path) -> Path:
    """A committed throwaway repository holding the script and this repo's own .gitignore (which
    ignores .claude/touch-goldens, as the real tree does)."""
    root = tmp_path / "repo"
    (root / SCRIPT_REL.parent).mkdir(parents=True)
    shutil.copy2(REPO_ROOT / SCRIPT_REL, root / SCRIPT_REL)
    shutil.copy2(REPO_ROOT / ".gitignore", root / ".gitignore")
    _git(root, "init", "-q")
    _git(root, "add", "-A")
    _git(root, "-c", "user.name=t", "-c", "user.email=t@example.com", "commit", "-qm", "seed")
    return root


def _run(tree: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["bash", str(tree / SCRIPT_REL)],
        cwd=tree,
        env=env,
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )


def _assert_nothing_happened(tree: Path, head_before: str) -> None:
    assert not (tree / "tests" / "golden").exists()
    assert not (tree / "build").exists()
    assert _git(tree, "rev-parse", "HEAD") == head_before


def test_refuses_when_unarmed(tree: Path) -> None:
    head = _git(tree, "rev-parse", "HEAD")
    result = _run(tree, _env())
    assert result.returncode != 0
    assert "unarmed" in result.stderr
    _assert_nothing_happened(tree, head)


def test_refuses_when_the_env_override_is_not_exactly_one(tree: Path) -> None:
    head = _git(tree, "rev-parse", "HEAD")
    result = _run(tree, _env(ASTRA_TOUCH_GOLDENS="0"))
    assert result.returncode != 0
    assert "unarmed" in result.stderr
    _assert_nothing_happened(tree, head)


def test_armed_but_dirty_tree_refuses_before_any_build(tree: Path) -> None:
    (tree / ".claude").mkdir()
    (tree / ".claude" / "touch-goldens").touch()
    (tree / "stray.txt").write_text("uncommitted\n", encoding="utf-8")
    head = _git(tree, "rev-parse", "HEAD")
    result = _run(tree, _env())
    assert result.returncode != 0
    assert "armed (.claude/touch-goldens)" in result.stdout
    assert "uncommitted changes" in result.stderr
    _assert_nothing_happened(tree, head)


def test_armed_clean_tree_without_the_baseline_refuses_before_any_build(tree: Path) -> None:
    # The throwaway history does not contain the baseline revision, so step 3 must stop it.
    head = _git(tree, "rev-parse", "HEAD")
    result = _run(tree, _env(ASTRA_TOUCH_GOLDENS="1"))
    assert result.returncode != 0
    assert "armed (ASTRA_TOUCH_GOLDENS=1)" in result.stdout
    assert "not an ancestor of HEAD" in result.stderr
    _assert_nothing_happened(tree, head)
