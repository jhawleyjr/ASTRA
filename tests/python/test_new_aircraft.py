"""scripts/new-aircraft.sh (docs/architecture.md §18, PLAN.md M3 PR 1): run against an isolated
temp root, never the live repo tree -- the script resolves its own repo root from its own
location (BASH_SOURCE), matching scripts/check-fast.sh's convention, so copying it plus
aircraft/_template into a temp directory and invoking it there is a faithful, self-contained test.
"""

import shutil
import subprocess
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
_SCRIPT = _REPO_ROOT / "scripts" / "new-aircraft.sh"
_TEMPLATE = _REPO_ROOT / "aircraft" / "_template"


def _make_root(tmp_path: Path) -> Path:
    root = tmp_path / "repo"
    (root / "scripts").mkdir(parents=True)
    shutil.copy2(_SCRIPT, root / "scripts" / "new-aircraft.sh")
    (root / "scripts" / "new-aircraft.sh").chmod(0o755)
    shutil.copytree(_TEMPLATE, root / "aircraft" / "_template")
    return root


def _run(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(root / "scripts" / "new-aircraft.sh"), *args],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )


def test_copies_template_and_rewrites_config_paths(tmp_path: Path) -> None:
    root = _make_root(tmp_path)
    result = _run(root, "gliderx")
    assert result.returncode == 0, result.stderr

    dest = root / "aircraft" / "gliderx"
    assert (dest / "physical" / "mass" / "mass.cpp").is_file()
    assert (dest / "physical" / "mass" / "CMakeLists.txt").is_file()

    config_text = (dest / "configs" / "cruise.yaml").read_text()
    assert "aircraft/gliderx/physical/mass" in config_text
    assert "aircraft/_template" not in config_text


def test_rewrites_the_aero_and_turbofan_math_namespace(tmp_path: Path) -> None:
    """docs/plans/pr4-notional-aero-turbofan.md (Joe's own review, real finding): aero_math.hpp/
    turbofan_math.hpp's Inputs/Coefficients/compute_wrench() live in an aircraft-specific C++
    namespace, not a generic one -- ASTRA_STATIC_PLUGINS links every plugin's object into one
    binary, so two aircraft sharing one namespace would be a silent One Definition Rule violation
    the instant a copied aircraft's own math header is edited to differ from _template's. The
    namespace token must be rewritten on copy, exactly like the YAML plugin paths."""
    root = _make_root(tmp_path)
    result = _run(root, "gliderx")
    assert result.returncode == 0, result.stderr

    dest = root / "aircraft" / "gliderx"
    aero_math_text = (dest / "physical" / "aero" / "aero_math.hpp").read_text()
    aero_text = (dest / "physical" / "aero" / "aero.cpp").read_text()
    turbofan_math_text = (
        dest / "hardware" / "propulsion" / "turbofan" / "turbofan_math.hpp"
    ).read_text()
    turbofan_text = (dest / "hardware" / "propulsion" / "turbofan" / "turbofan.cpp").read_text()

    assert "astra::aircraft_gliderx_aero_math" in aero_math_text
    assert "astra::aircraft_gliderx_aero_math" in aero_text
    assert "astra::aircraft_gliderx_turbofan_math" in turbofan_math_text
    assert "astra::aircraft_gliderx_turbofan_math" in turbofan_text
    assert "aircraft_template_aero_math" not in aero_math_text
    assert "aircraft_template_aero_math" not in aero_text
    assert "aircraft_template_turbofan_math" not in turbofan_math_text
    assert "aircraft_template_turbofan_math" not in turbofan_text


def test_rejects_a_name_with_disallowed_characters(tmp_path: Path) -> None:
    root = _make_root(tmp_path)
    result = _run(root, "Not-Valid!")
    assert result.returncode != 0
    assert not (root / "aircraft" / "Not-Valid!").exists()


def test_refuses_to_overwrite_an_existing_aircraft_directory(tmp_path: Path) -> None:
    root = _make_root(tmp_path)
    (root / "aircraft" / "existing").mkdir()
    (root / "aircraft" / "existing" / "sentinel.txt").write_text("do not touch")

    result = _run(root, "existing")
    assert result.returncode != 0
    assert (root / "aircraft" / "existing" / "sentinel.txt").read_text() == "do not touch"


def test_refuses_the_template_name_itself(tmp_path: Path) -> None:
    root = _make_root(tmp_path)
    result = _run(root, "_template")
    assert result.returncode != 0


def test_justfile_recipe_quotes_the_name_argument() -> None:
    """Codex adversarial review, PLAN.md M3 PR 1: `just` substitutes a recipe parameter into the
    command text before bash ever parses it, so a bare {{name}} would let a crafted name like
    '$(cmd)x' run `cmd` via this recipe's own shell before new-aircraft.sh's own regex ever sees
    it. quote(name) single-quote-escapes the value so it reaches the script as inert text."""
    justfile_text = (_REPO_ROOT / "justfile").read_text()
    assert "./scripts/new-aircraft.sh {{quote(name)}}" in justfile_text


def test_just_recipe_rejects_a_shell_injection_shaped_name(tmp_path: Path) -> None:
    """End-to-end regression for the same finding, through the real `just` + justfile + script (not
    the isolated temp root above, which calls the script directly and would miss this): the
    injected command must never run, and the name is invalid so no aircraft/ directory is created,
    needing no cleanup of the live repo."""
    sentinel = tmp_path / "sentinel"
    malicious = f"$(touch {sentinel})x"
    result = subprocess.run(
        ["just", "new-aircraft", malicious],
        cwd=_REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode != 0
    assert not sentinel.exists()
    assert not (_REPO_ROOT / "aircraft" / malicious).exists()
