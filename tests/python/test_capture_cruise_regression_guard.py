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
- Halt, cross-preset-mismatch and existing-golden (J-2) refusals need the baseline in history, so
  they run in a `git clone --shared` of this repository with `cmake` replaced on PATH by a stub
  that builds nothing and writes a stub harness; the real script then drives the stub through
  steps 4-5. HEAD carries the captured golden since commit B, so the halt/mismatch cases commit
  its removal in the clone first -- the only way past step 1b.
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
# Every invocation's arguments are appended, one line each, to STUB_CMAKE_LOG when it is set.
if [[ -n "${STUB_CMAKE_LOG:-}" ]]; then
  echo "$*" >>"$STUB_CMAKE_LOG"
fi
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

    stub_bin = tmp_path / "bin"
    stub_bin.mkdir()
    (stub_bin / "cmake").write_text(_STUB_CMAKE, encoding="utf-8")
    (stub_bin / "cmake").chmod(0o755)
    env = _env(ASTRA_TOUCH_GOLDENS="1", PATH=f"{stub_bin}{os.pathsep}{os.environ['PATH']}")
    return clone, env


def _golden_snapshot(clone: Path) -> dict[str, bytes] | None:
    """Every file under the golden directory, by relative path, or None if the directory does not
    exist. Before capture it does not; after capture (commit B) it holds the committed header and
    manifest (Codex R-002) -- either way a refusal must leave it exactly as it was."""
    golden = clone / GOLDEN_REL
    if not golden.exists():
        return None
    return {
        str(path.relative_to(golden)): path.read_bytes()
        for path in sorted(golden.rglob("*"))
        if path.is_file()
    }


def _assert_goldens_untouched_and_no_commit(
    clone: Path, head_before: str, goldens_before: dict[str, bytes] | None
) -> None:
    assert _golden_snapshot(clone) == goldens_before
    assert _git(clone, "rev-parse", "HEAD") == head_before
    assert _git(clone, "status", "--porcelain") == ""


def _remove_golden(clone: Path) -> None:
    """Commit the golden's removal in the clone, so the script gets past step 1b (J-2) and reaches
    the refusal under test. Since commit B, HEAD carries the captured golden."""
    if (clone / GOLDEN_REL).exists():
        _git(clone, "rm", "-rq", str(GOLDEN_REL))
        _commit_all(clone, "remove golden for a steps 4-5 refusal test")


def test_refuses_to_write_when_the_run_halts(armed_clone: tuple[Path, dict[str, str]]) -> None:
    clone, env = armed_clone
    _remove_golden(clone)
    head = _git(clone, "rev-parse", "HEAD")
    result = _run(clone, {**env, "STUB_HARNESS_MODE": "halt"})
    assert result.returncode != 0
    assert "harness exited 2" in result.stderr
    _assert_goldens_untouched_and_no_commit(clone, head, None)


def test_refuses_to_write_on_a_cross_preset_mismatch(
    armed_clone: tuple[Path, dict[str, str]],
) -> None:
    clone, env = armed_clone
    _remove_golden(clone)
    head = _git(clone, "rev-parse", "HEAD")
    result = _run(clone, {**env, "STUB_HARNESS_MODE": "mismatch"})
    assert result.returncode != 0
    assert "DISAGREEMENT between debug and release" in result.stderr
    assert "cross-preset disagreement" in result.stderr
    _assert_goldens_untouched_and_no_commit(clone, head, None)


@pytest.mark.parametrize(
    "present",
    [
        ("cruise_regression_golden.hpp", "cruise_regression_manifest.md"),
        ("cruise_regression_golden.hpp",),
        ("cruise_regression_manifest.md",),
    ],
    ids=["header-and-manifest", "header-only", "manifest-only"],
)
def test_armed_refuses_when_a_golden_already_exists(
    armed_clone: tuple[Path, dict[str, str]], present: tuple[str, ...]
) -> None:
    # J-2 (Joe): arming never authorizes a re-capture. With the clone armed, clean and holding the
    # baseline -- everything else the script checks satisfied -- an existing golden header or
    # manifest alone must stop it before any build (the stub cmake never runs, so no build/), and
    # leave the existing files byte-identical. Also covers R-002: the fixture itself works at a HEAD
    # that already carries a committed golden.
    clone, env = armed_clone
    _remove_golden(clone)
    golden = clone / GOLDEN_REL
    golden.mkdir(parents=True)
    for name in present:
        (golden / name).write_text(f"// existing {name}\n", encoding="utf-8")
    _commit_all(clone, "existing golden")
    head = _git(clone, "rev-parse", "HEAD")
    goldens = _golden_snapshot(clone)
    result = _run(clone, {**env, "STUB_HARNESS_MODE": "mismatch"})
    assert result.returncode != 0
    assert "armed (ASTRA_TOUCH_GOLDENS=1)" in result.stdout
    assert "a golden already exists" in result.stderr
    assert not (clone / "build").exists()
    _assert_goldens_untouched_and_no_commit(clone, head, goldens)


def test_authorized_recapture_builds_only_the_harness_target(
    armed_clone: tuple[Path, dict[str, str]], tmp_path: Path
) -> None:
    # R-003 (Joe-authorized fix): the re-capture path J-2 documents -- golden removed in a commit,
    # session armed -- must reach step 4 and request ONLY the harness target, because a full build
    # would compile the regression test, which includes the (now removed) golden header. Observed
    # through the stub cmake's own invocation log on the real script path, not by reading the
    # script's text; the stub mismatch mode then stops the run at step 5, before any write.
    clone, env = armed_clone
    _remove_golden(clone)
    head = _git(clone, "rev-parse", "HEAD")
    log = tmp_path / "cmake-invocations.log"
    result = _run(clone, {**env, "STUB_HARNESS_MODE": "mismatch", "STUB_CMAKE_LOG": str(log)})

    assert "armed (ASTRA_TOUCH_GOLDENS=1)" in result.stdout
    assert "no existing golden." in result.stdout  # J-2 did not fire
    assert "a golden already exists" not in result.stderr
    invocations = log.read_text(encoding="utf-8").splitlines()
    configures = [line for line in invocations if line.startswith("--preset ")]
    builds = [line.split() for line in invocations if line.startswith("--build ")]
    presets = ["debug", "release", "asan", "static-plugins"]
    assert configures == [f"--preset {p}" for p in presets]
    assert [b[1] for b in builds] == [f"build/{p}" for p in presets]  # one build per preset
    for build in builds:
        assert build[2:4] == ["--target", "capture_cruise_regression"], build
        assert build.count("--target") == 1, build
    assert "cross-preset disagreement" in result.stderr  # reached step 5 on the stub's output
    _assert_goldens_untouched_and_no_commit(clone, head, None)
