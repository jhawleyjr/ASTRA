#!/usr/bin/env python3
"""Per-session, per-model token usage from Claude Code transcripts (workflow.md, Model and effort).

Reads every ``*.jsonl`` transcript under the given roots (default: ``~/.claude/projects/*ASTRA*``),
sums each assistant message's ``usage`` fields per (session, model) pair, and prints one row per
pair plus a total row -- so a session that switches models under `opusplan` (Opus in plan mode,
Sonnet once a plan is approved) shows the split workflow.md §Model and effort measures with. Used
to measure cost after a PR, and by ``/retro`` step 4 each milestone. Stdlib only.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path


@dataclass
class ModelUsage:
    """Aggregated token counts for one (session, model) pair."""

    session_id: str
    model: str
    input_tokens: int = 0
    output_tokens: int = 0
    cache_read_tokens: int = 0
    cache_creation_tokens: int = 0

    def add(self, usage: dict[str, object]) -> None:
        self.input_tokens += _as_int(usage.get("input_tokens"))
        self.output_tokens += _as_int(usage.get("output_tokens"))
        self.cache_read_tokens += _as_int(usage.get("cache_read_input_tokens"))
        self.cache_creation_tokens += _as_int(usage.get("cache_creation_input_tokens"))


def _as_int(value: object) -> int:
    return int(value) if isinstance(value, int | float) else 0


def _parse_timestamp(value: str) -> datetime:
    # Transcripts use ISO 8601 UTC with a trailing "Z"; Python 3.11+ parses that directly.
    return datetime.fromisoformat(value)


def find_transcripts(roots: list[str]) -> list[Path]:
    """Every *.jsonl under each root, which may itself be a glob (e.g. the default ``*ASTRA*``)."""
    paths: set[Path] = set()
    for root in roots:
        expanded = os.path.expanduser(root)
        paths.update(Path(p) for p in glob.glob(f"{expanded}/**/*.jsonl", recursive=True))
    return sorted(paths)


def collect_usage(paths: list[Path], since: datetime | None) -> dict[tuple[str, str], ModelUsage]:
    rows: dict[tuple[str, str], ModelUsage] = {}
    for path in paths:
        with path.open(encoding="utf-8") as handle:
            for raw_line in handle:
                stripped = raw_line.strip()
                if not stripped:
                    continue
                try:
                    record = json.loads(stripped)
                except json.JSONDecodeError:
                    continue
                if record.get("type") != "assistant":
                    continue
                timestamp = record.get("timestamp")
                if (
                    since is not None
                    and timestamp is not None
                    and _parse_timestamp(timestamp) < since
                ):
                    continue
                session_id = record.get("sessionId")
                message = record.get("message")
                if not session_id or not isinstance(message, dict):
                    continue
                usage = message.get("usage")
                if not isinstance(usage, dict):
                    continue
                model = message.get("model") or "-"
                key = (session_id, model)
                rows.setdefault(key, ModelUsage(session_id, model)).add(usage)
    return rows


_COLUMNS = ("session", "model", "output", "cache_read", "cache_create", "input")
_ROW_FORMAT = "{:<38}{:<20}{:>12}{:>14}{:>14}{:>10}"


def _row(session_id: str, model: str, usage: ModelUsage) -> str:
    return _ROW_FORMAT.format(
        session_id,
        model,
        usage.output_tokens,
        usage.cache_read_tokens,
        usage.cache_creation_tokens,
        usage.input_tokens,
    )


def format_table(rows: dict[tuple[str, str], ModelUsage]) -> str:
    header = _ROW_FORMAT.format(*_COLUMNS)
    rule = "-" * len(header)
    lines = [header, rule]

    total = ModelUsage("TOTAL", "")
    for key in sorted(rows):
        usage = rows[key]
        lines.append(_row(usage.session_id, usage.model, usage))
        total.input_tokens += usage.input_tokens
        total.output_tokens += usage.output_tokens
        total.cache_read_tokens += usage.cache_read_tokens
        total.cache_creation_tokens += usage.cache_creation_tokens

    lines.append(rule)
    lines.append(_row("TOTAL", "", total))
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "roots",
        nargs="*",
        default=["~/.claude/projects/*ASTRA*"],
        help="glob(s) of project directories to search for *.jsonl transcripts",
    )
    parser.add_argument(
        "--since",
        default=None,
        help="only count assistant messages at or after this ISO 8601 timestamp",
    )
    args = parser.parse_args(argv)
    since = _parse_timestamp(args.since).astimezone(UTC) if args.since else None
    rows = collect_usage(find_transcripts(args.roots), since)
    print(format_table(rows))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
