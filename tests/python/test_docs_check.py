"""scripts/docs_check.py's link/anchor checker: same-page and cross-file anchors."""

import importlib.util
import sys
from pathlib import Path
from types import ModuleType

_SCRIPT_PATH = Path(__file__).resolve().parents[2] / "scripts" / "docs_check.py"


def _load_docs_check_module() -> ModuleType:
    spec = importlib.util.spec_from_file_location("docs_check", _SCRIPT_PATH)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


docs_check = _load_docs_check_module()


def test_same_page_anchor_that_exists_passes(tmp_path: Path) -> None:
    f = tmp_path / "doc.md"
    f.write_text("# Title\n\n## A Real Heading\n\nSee [it](#a-real-heading).\n")
    problems: list[str] = []
    docs_check.check_links([f], tmp_path, problems)
    assert problems == []


def test_same_page_anchor_that_is_missing_fails(tmp_path: Path) -> None:
    f = tmp_path / "doc.md"
    f.write_text("# Title\n\nSee [it](#does-not-exist).\n")
    problems: list[str] = []
    docs_check.check_links([f], tmp_path, problems)
    assert len(problems) == 1
    assert "does-not-exist" in problems[0]


def test_cross_file_anchor_that_exists_passes(tmp_path: Path) -> None:
    target = tmp_path / "other.md"
    target.write_text("# Other\n\n## Some Section\n")
    src = tmp_path / "doc.md"
    src.write_text("See [it](other.md#some-section).\n")
    problems: list[str] = []
    docs_check.check_links([src, target], tmp_path, problems)
    assert problems == []


def test_cross_file_anchor_that_is_missing_fails(tmp_path: Path) -> None:
    target = tmp_path / "other.md"
    target.write_text("# Other\n\n## Some Section\n")
    src = tmp_path / "doc.md"
    src.write_text("See [it](other.md#not-a-real-section).\n")
    problems: list[str] = []
    docs_check.check_links([src, target], tmp_path, problems)
    assert len(problems) == 1
    assert "not-a-real-section" in problems[0]


def test_broken_link_target_is_still_caught(tmp_path: Path) -> None:
    src = tmp_path / "doc.md"
    src.write_text("See [it](missing.md).\n")
    problems: list[str] = []
    docs_check.check_links([src], tmp_path, problems)
    assert len(problems) == 1
    assert "missing.md" in problems[0]


def test_a_pure_hash_link_is_not_flagged(tmp_path: Path) -> None:
    """`[text](#)` is a common no-op link convention, not a broken anchor."""
    f = tmp_path / "doc.md"
    f.write_text("# Title\n\nSee [it](#).\n")
    problems: list[str] = []
    docs_check.check_links([f], tmp_path, problems)
    assert problems == []


# STATE.md's shape: exactly the allowed `##` headings in order, at most 40 lines, the honesty rules verbatim.


def _state(*sections: tuple[str, str]) -> str:
    body = "".join(f"## {h}\n{text}\n\n" for h, text in sections)
    return "# STATE (written at /wrap as main reads it after the merge)\n\n" + body


def _good_sections() -> list[tuple[str, str]]:
    return [
        ("Next step", "1. M6 PR 6 or PR 9."),
        ("Open threads", "- PLAN.md's (M6) open items."),
        ("Honesty rules", docs_check.STATE_HONESTY_RULES),
    ]


def _check_state(tmp_path: Path, text: str) -> list[str]:
    (tmp_path / "STATE.md").write_text(text)
    problems: list[str] = []
    docs_check.check_state(tmp_path, problems)
    return problems


def test_state_with_the_allowed_headings_passes(tmp_path: Path) -> None:
    assert _check_state(tmp_path, _state(*_good_sections())) == []


def test_state_with_an_in_flight_section_fails(tmp_path: Path) -> None:
    """The old /wrap template's section: a PR's stage, false on main once the PR merges."""
    sections = [("In flight", "M6 PR 5, plots: PR #89. Stage: human review."), *_good_sections()]
    problems = _check_state(tmp_path, _state(*sections))
    assert len(problems) == 1
    assert "In flight" in problems[0]


def test_state_with_the_headings_out_of_order_fails(tmp_path: Path) -> None:
    a, b, c = _good_sections()
    problems = _check_state(tmp_path, _state(b, a, c))
    assert len(problems) == 1
    assert "in that order" in problems[0]


def test_state_with_a_missing_heading_fails(tmp_path: Path) -> None:
    a, _, c = _good_sections()
    assert len(_check_state(tmp_path, _state(a, c))) == 1


def test_state_over_the_line_cap_fails(tmp_path: Path) -> None:
    a, _, c = _good_sections()
    text = _state(a, ("Open threads", "\n".join(f"- item {i}" for i in range(40))), c)
    problems = _check_state(tmp_path, text)
    assert len(problems) == 1
    assert "40-line cap" in problems[0]


def test_state_with_different_honesty_rules_fails(tmp_path: Path) -> None:
    a, b, _ = _good_sections()
    text = _state(a, b, ("Honesty rules", '"A trace matched" is "verified" when it looks right.'))
    problems = _check_state(tmp_path, text)
    assert len(problems) == 1
    assert "STATE_HONESTY_RULES" in problems[0]


def test_state_heading_inside_a_fenced_block_is_not_a_section(tmp_path: Path) -> None:
    a, _, c = _good_sections()
    fenced = ("Open threads", "- an example:\n\n```\n## In flight\n```")
    assert _check_state(tmp_path, _state(a, fenced, c)) == []


def test_missing_state_fails(tmp_path: Path) -> None:
    problems: list[str] = []
    docs_check.check_state(tmp_path, problems)
    assert problems == ["STATE.md: missing"]


def test_the_repository_state_passes() -> None:
    problems: list[str] = []
    docs_check.check_state(_SCRIPT_PATH.parents[1], problems)
    assert problems == []
