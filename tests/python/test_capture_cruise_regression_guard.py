"""tests/capture/capture_cruise_regression.sh's own refusals (M3 PR 8, D20,
docs/plans/pr8-cruise-regression.md): the guard, not the happy path.

Every case runs a byte-identical copy of the working tree's script at its real relative path inside
a throwaway repository under tmp_path, so the script's own ROOT (resolved from its location) is that
repository -- never this checkout, never the main checkout's .claude/touch-goldens marker. Every git
and script subprocess gets an environment with ASTRA_TOUCH_GOLDENS and every GIT_* variable removed
(Codex adversarial review R-001: an inherited GIT_DIR/GIT_WORK_TREE would otherwise redirect the
fixture's own init/add/commit into a real checkout), so a session Joe has armed still sees the
unarmed cases as unarmed.

- Arming and tree/baseline refusals run in a bare `git init` tree with no CMakePresets.json, so even
  a guard regression could not start a real capture.
- Halt and cross-preset-mismatch refusals need the baseline in history, so they run in a
  `git clone --shared` of this repository with `cmake` replaced on PATH by a stub that builds
  nothing and writes a stub harness; the real script then drives the stub through steps 4-5.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_REL = Path("tests/capture/capture_cruise_regression.sh")
GOLDEN_REL = Path("tests/golden/notional_transport")


def _env(**overrides: str) -> dict[str, str]:
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.pop("ASTRA_TOUCH_GOLDENS", None)
    env.update(overrides)
    return env


def _git(tree: Path, *args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(tree), *args], check=True, capture_output=True, text=True, env=_env()
    ).stdout


def _commit_all(tree: Path, message: str) -> None:
    _git(tree, "add", "-A")
    _git(tree, "-c", "user.name=t", "-c", "user.email=t@example.com", "commit", "-qm", message)


def _make_tree(tmp_path: Path) -> Path:
    """A committed throwaway repository holding the script and this repo's own .gitignore (which
    ignores .claude/touch-goldens, as the real tree does)."""
    root = tmp_path / "repo"
    (root / SCRIPT_REL.parent).mkdir(parents=True)
    shutil.copy2(REPO_ROOT / SCRIPT_REL, root / SCRIPT_REL)
    shutil.copy2(REPO_ROOT / ".gitignore", root / ".gitignore")
    _git(root, "init", "-q")
    _commit_all(root, "seed")
    return root


@pytest.fixture
def tree(tmp_path: Path) -> Path:
    return _make_tree(tmp_path)


def _run(
    tree: Path, env: dict[str, str], timeout_s: float = 60.0
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["bash", str(tree / SCRIPT_REL)],
        cwd=tree,
        env=env,
        capture_output=True,
        text=True,
        timeout=timeout_s,
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


def test_inherited_git_variables_cannot_redirect_the_fixture(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # R-001: with GIT_DIR/GIT_WORK_TREE inherited from the caller pointing at another repository,
    # building the throwaway tree and running the script must leave that repository untouched.
    decoy = tmp_path / "decoy"
    decoy.mkdir()
    (decoy / "tracked.txt").write_text("decoy\n", encoding="utf-8")
    _git(decoy, "init", "-q")
    _commit_all(decoy, "decoy")
    (decoy / "untracked.txt").write_text("must stay untracked\n", encoding="utf-8")
    head_before = _git(decoy, "rev-parse", "HEAD")
    status_before = _git(decoy, "status", "--porcelain")

    monkeypatch.setenv("GIT_DIR", str(decoy / ".git"))
    monkeypatch.setenv("GIT_WORK_TREE", str(decoy))
    work = tmp_path / "work"
    work.mkdir()
    built = _make_tree(work)
    _run(built, _env())

    assert _git(decoy, "rev-parse", "HEAD") == head_before
    assert _git(decoy, "status", "--porcelain") == status_before
    assert _git(built, "log", "--format=%s") == "seed\n"


# --- Steps 4-5 against a stub build: halt and cross-preset mismatch. ---

_STUB_CMAKE = """#!/usr/bin/env bash
# Stub cmake for test_capture_cruise_regression_guard.py: builds nothing. For
# `--build build/<preset> --target capture_cruise_regression ...` it writes a stub harness whose
# `trajectory` behavior is chosen by STUB_HARNESS_MODE (halt: exit 2; mismatch: preset-dependent).
if [[ "${1:-}" == "--build" && "${3:-}" == "--target" ]]; then
  preset="$(basename "$2")"
  mkdir -p "$2/tests/capture"
  harness="$2/tests/capture/capture_cruise_regression"
  cat >"$harness" <<EOF
#!/usr/bin/env bash
if [[ "\\$1" == meta ]]; then echo EIGEN_VERSION=stub; exit 0; fi
case "\\${STUB_HARNESS_MODE:-}" in
  halt) echo "stub: run halted" >&2; exit 2 ;;
  mismatch) echo "trajectory from ${preset}"; exit 0 ;;
  *) echo "stub: unknown STUB_HARNESS_MODE" >&2; exit 3 ;;
esac
EOF
  chmod +x "$harness"
fi
exit 0
"""


@pytest.fixture
def armed_clone(tmp_path: Path) -> tuple[Path, dict[str, str]]:
    """A shared clone of this repository at this checkout's HEAD (so the baseline is in history),
    with the working tree's own script committed on top if it differs, a clean tree, and an
    environment that is armed and puts the stub cmake first on PATH."""
    clone = tmp_path / "clone"
    subprocess.run(
        ["git", "clone", "-q", "--shared", "--no-checkout", str(REPO_ROOT), str(clone)],
        check=True,
        capture_output=True,
        env=_env(),
    )
    _git(clone, "checkout", "-q", _git(REPO_ROOT, "rev-parse", "HEAD").strip())
    shutil.copy2(REPO_ROOT / SCRIPT_REL, clone / SCRIPT_REL)
    if _git(clone, "status", "--porcelain"):
        _commit_all(clone, "working-tree capture script")
    assert not (clone / GOLDEN_REL).exists()

    stub_bin = tmp_path / "bin"
    stub_bin.mkdir()
    (stub_bin / "cmake").write_text(_STUB_CMAKE, encoding="utf-8")
    (stub_bin / "cmake").chmod(0o755)
    env = _env(ASTRA_TOUCH_GOLDENS="1", PATH=f"{stub_bin}{os.pathsep}{os.environ['PATH']}")
    return clone, env


def _assert_no_golden_and_no_commit(clone: Path, head_before: str) -> None:
    assert not (clone / GOLDEN_REL).exists()
    assert _git(clone, "rev-parse", "HEAD") == head_before
    assert _git(clone, "status", "--porcelain") == ""


def test_refuses_to_write_when_the_run_halts(armed_clone: tuple[Path, dict[str, str]]) -> None:
    clone, env = armed_clone
    head = _git(clone, "rev-parse", "HEAD")
    result = _run(clone, {**env, "STUB_HARNESS_MODE": "halt"})
    assert result.returncode != 0
    assert "harness exited 2" in result.stderr
    _assert_no_golden_and_no_commit(clone, head)


def test_refuses_to_write_on_a_cross_preset_mismatch(
    armed_clone: tuple[Path, dict[str, str]],
) -> None:
    clone, env = armed_clone
    head = _git(clone, "rev-parse", "HEAD")
    result = _run(clone, {**env, "STUB_HARNESS_MODE": "mismatch"})
    assert result.returncode != 0
    assert "DISAGREEMENT between debug and release" in result.stderr
    assert "cross-preset disagreement" in result.stderr
    _assert_no_golden_and_no_commit(clone, head)
