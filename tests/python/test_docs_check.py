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
