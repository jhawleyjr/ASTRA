#!/usr/bin/env python3
"""Reproducible check for the workflow constraints docs/process/validation.md prescribes for
workflow edits (Markdown links/anchors, SKILL.md metadata, the root instruction line budget,
doc/script command names, and STATE.md's shape) so they run from `just docs-check` / CI instead
of being asked of an agent's memory. Stdlib only.

Usage: python3 scripts/docs_check.py [--root DIR]
Exit 0 with a summary line, or exit 1 printing every problem found.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
HEADING_RE = re.compile(r"^#{1,6}\s+(.*)")
JUST_RECIPE_REF_RE = re.compile(r"`just ([A-Za-z][A-Za-z0-9_-]*)")
# Only the repo's own `./scripts/...` convention (docs/process/validation.md, AGENTS.md,
# README.md all write it this way); a bare `scripts/...` may name a plugin's own script
# (e.g. docs/agents/review-dispatch.md's Codex companion), which this repo does not own.
SCRIPT_REF_RE = re.compile(r"`\./scripts/([A-Za-z0-9_.-]+)")
FENCE_RE = re.compile(r"^ {0,3}(`{3,}|~{3,})(.*)$")
JUSTFILE_RECIPE_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_-]*)\b[^\n:]*:(?!=)")
ROOT_INSTRUCTION_LINE_BUDGET = 100

# The workflow's own documentation surface (docs/process/validation.md's "for workflow
# edits" scope): process/agent/template docs and skills. Deliberately excludes
# docs/architecture.md and docs/decisions/** -- Joe-owned design docs that may intentionally
# reference a future/planned command not yet in the justfile (as `just new-aircraft` was before
# M3 PR 1); those are not "workflow edits" and a mechanical check should not flag them.
WORKFLOW_DOC_ROOTS = [
    "AGENTS.md",
    "CLAUDE.md",
    "README.md",
    "docs/README.md",
    "docs/process",
    "docs/agents",
    "docs/templates",
    "docs/reviews/README.md",
    ".claude/skills",
]

# STATE.md is written as main reads it after the squash-merge (.claude/skills/wrap/SKILL.md
# step 5), so its shape is checked, not its wording: exactly these `##` headings, in this order,
# at most 40 lines, and the honesty rules word for word. This constant is the one source of that
# text.
STATE_HEADINGS = ["Next step", "Open threads", "Honesty rules"]
STATE_LINE_CAP = 40
STATE_HONESTY_RULES = (
    '"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 '
    "in this session."
)

# Checked for links/anchors only: PLAN.md links every PR's plan and packet, but as a plan it may
# name a future command (the reason the design docs above are excluded), so its command names
# are not checked.
LINK_ONLY_DOC_FILES = ["PLAN.md"]


def iter_workflow_doc_files(root: Path) -> list[Path]:
    out = []
    for rel in WORKFLOW_DOC_ROOTS:
        p = root / rel
        if p.is_file() and p.suffix == ".md":
            out.append(p)
        elif p.is_dir():
            out.extend(sorted(q for q in p.rglob("*.md")))
    return sorted(set(out))


def slugify(heading: str) -> str:
    s = heading.strip().lower()
    s = re.sub(r"[^\w\s-]", "", s)
    s = re.sub(r"\s+", "-", s)
    return s


def anchors_of(path: Path) -> set[str]:
    seen: dict[str, int] = {}
    slugs = set()
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return slugs
    for line in text.splitlines():
        m = HEADING_RE.match(line)
        if not m:
            continue
        base = slugify(m.group(1))
        n = seen.get(base, 0)
        seen[base] = n + 1
        slugs.add(base if n == 0 else f"{base}-{n}")
    return slugs


def check_links(md_files: list[Path], root: Path, problems: list[str]) -> None:
    for f in md_files:
        text = f.read_text(encoding="utf-8", errors="replace")
        for m in LINK_RE.finditer(text):
            target = m.group(1).strip()
            if not target or target.startswith(("http://", "https://", "mailto:")):
                continue
            path_part, _, frag = target.partition("#")
            if not path_part:
                # Pure in-page fragment (e.g. [Section](#does-not-exist)): validate against
                # this same file's own headings instead of skipping it.
                if frag and frag not in anchors_of(f):
                    problems.append(f"{f.relative_to(root)}: broken same-page anchor '#{frag}'")
                continue
            resolved = (f.parent / path_part).resolve()
            if not resolved.exists():
                problems.append(f"{f.relative_to(root)}: broken link target '{target}'")
                continue
            if (
                frag
                and resolved.is_file()
                and resolved.suffix == ".md"
                and frag not in anchors_of(resolved)
            ):
                problems.append(f"{f.relative_to(root)}: broken anchor '#{frag}' in '{path_part}'")


def check_skills(root: Path, problems: list[str]) -> None:
    skills_dir = root / ".claude" / "skills"
    if not skills_dir.is_dir():
        return
    for skill_dir in sorted(p for p in skills_dir.iterdir() if p.is_dir()):
        skill_md = skill_dir / "SKILL.md"
        if not skill_md.is_file():
            problems.append(f"{skill_dir.relative_to(root)}: missing SKILL.md")
            continue
        text = skill_md.read_text(encoding="utf-8")
        fm = re.match(r"^---\n(.*?)\n---\n", text, re.DOTALL)
        if not fm:
            problems.append(f"{skill_md.relative_to(root)}: no frontmatter block")
            continue
        name_m = re.search(r"^name:\s*(\S+)\s*$", fm.group(1), re.MULTILINE)
        desc_m = re.search(r"^description:\s*(.+\S)\s*$", fm.group(1), re.MULTILINE)
        if not name_m:
            problems.append(f"{skill_md.relative_to(root)}: frontmatter missing 'name'")
        elif name_m.group(1) != skill_dir.name:
            problems.append(
                f"{skill_md.relative_to(root)}: frontmatter name "
                f"'{name_m.group(1)}' does not match directory '{skill_dir.name}'"
            )
        if not desc_m:
            problems.append(f"{skill_md.relative_to(root)}: frontmatter missing 'description'")


def check_root_instruction_budget(root: Path, problems: list[str]) -> None:
    agents = root / "AGENTS.md"
    claude = root / "CLAUDE.md"
    if not agents.is_file() or not claude.is_file():
        return
    n = len(agents.read_text(encoding="utf-8").splitlines()) + len(
        claude.read_text(encoding="utf-8").splitlines()
    )
    if n > ROOT_INSTRUCTION_LINE_BUDGET:
        problems.append(
            f"AGENTS.md + CLAUDE.md is {n} lines, over the "
            f"{ROOT_INSTRUCTION_LINE_BUDGET}-line budget"
        )


def check_state(root: Path, problems: list[str]) -> None:
    state = root / "STATE.md"
    if not state.is_file():
        problems.append("STATE.md: missing")
        return
    lines = state.read_text(encoding="utf-8").splitlines()
    if len(lines) > STATE_LINE_CAP:
        problems.append(f"STATE.md: {len(lines)} lines, over the {STATE_LINE_CAP}-line cap")
    headings: list[str] = []
    sections: dict[str, list[str]] = {}
    # The open fence's marker run. As in CommonMark, only a run of the same character, at least as
    # long and with nothing after it, closes it.
    fence = ""
    for line in lines:
        m = FENCE_RE.match(line)
        if m and not fence:
            fence = m.group(1)
            continue
        run, rest = (m.group(1), m.group(2)) if m else ("", "")
        if run and run[0] == fence[0] and len(run) >= len(fence) and not rest.strip():
            fence = ""
            continue
        if not fence and line.startswith("## "):
            headings.append(line[3:].strip())
            sections[headings[-1]] = []
        elif headings:
            sections[headings[-1]].append(line)
    if headings != STATE_HEADINGS:
        problems.append(
            f"STATE.md: '##' headings are {headings}, "
            f"expected exactly {STATE_HEADINGS} in that order"
        )
    honesty = sections.get("Honesty rules")
    if honesty is not None and "\n".join(honesty).strip() != STATE_HONESTY_RULES:
        problems.append(
            "STATE.md: the Honesty rules section differs from "
            "scripts/docs_check.py's STATE_HONESTY_RULES"
        )


def parse_justfile_recipes(root: Path) -> set[str]:
    justfile = root / "justfile"
    if not justfile.is_file():
        return set()
    names = set()
    for line in justfile.read_text(encoding="utf-8").splitlines():
        if not line or line[0].isspace() or line.lstrip().startswith("#"):
            continue
        m = JUSTFILE_RECIPE_RE.match(line)
        if m and m.group(1) not in {"set", "export"}:
            names.add(m.group(1))
    return names


def check_command_names(md_files: list[Path], root: Path, problems: list[str]) -> None:
    recipes = parse_justfile_recipes(root)
    for f in md_files:
        text = f.read_text(encoding="utf-8", errors="replace")
        for m in JUST_RECIPE_REF_RE.finditer(text):
            name = m.group(1)
            if recipes and name not in recipes:
                problems.append(f"{f.relative_to(root)}: references unknown recipe 'just {name}'")
        for m in SCRIPT_REF_RE.finditer(text):
            name = m.group(1)
            if not (root / "scripts" / name).is_file():
                problems.append(f"{f.relative_to(root)}: references missing 'scripts/{name}'")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="repository root (default: cwd)")
    args = parser.parse_args()
    root = Path(args.root).resolve()

    md_files = iter_workflow_doc_files(root)
    link_only = [root / rel for rel in LINK_ONLY_DOC_FILES if (root / rel).is_file()]
    problems: list[str] = []
    check_links(md_files + link_only, root, problems)
    check_skills(root, problems)
    check_root_instruction_budget(root, problems)
    check_command_names(md_files, root, problems)
    check_state(root, problems)

    if problems:
        print(f"docs-check: {len(problems)} problem(s) found:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1
    print(f"docs-check: OK ({len(md_files) + len(link_only)} Markdown files checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
