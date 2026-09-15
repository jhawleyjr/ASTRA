"""scripts/tokens.py against a synthetic transcript: per-session sums, the total row, --since."""

import importlib.util
import json
import sys
from datetime import UTC, datetime
from pathlib import Path
from types import ModuleType

_SCRIPT_PATH = Path(__file__).resolve().parents[2] / "scripts" / "tokens.py"


def _load_tokens_module() -> ModuleType:
    spec = importlib.util.spec_from_file_location("tokens", _SCRIPT_PATH)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    # dataclasses (tokens.py uses one) resolves stringified annotations via
    # sys.modules[cls.__module__], so the module must be registered before exec_module runs.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


tokens = _load_tokens_module()


def _assistant_record(session_id: str, timestamp: str, model: str, **usage: int) -> str:
    record = {
        "type": "assistant",
        "sessionId": session_id,
        "timestamp": timestamp,
        "message": {"model": model, "usage": usage},
    }
    return json.dumps(record)


def _write_transcript(path: Path, lines: list[str]) -> None:
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def test_sums_usage_per_session_and_ignores_non_assistant_records(tmp_path: Path) -> None:
    transcript = tmp_path / "session-a.jsonl"
    _write_transcript(
        transcript,
        [
            json.dumps({"type": "summary", "sessionId": "a"}),
            _assistant_record(
                "a",
                "2026-09-14T08:00:00Z",
                "claude-opus-5",
                input_tokens=2,
                output_tokens=100,
                cache_read_input_tokens=1000,
                cache_creation_input_tokens=500,
            ),
            _assistant_record(
                "a",
                "2026-09-14T08:01:00Z",
                "claude-opus-5",
                input_tokens=3,
                output_tokens=50,
                cache_read_input_tokens=2000,
                cache_creation_input_tokens=0,
            ),
        ],
    )

    sessions = tokens.collect_usage(tokens.find_transcripts([str(tmp_path)]), since=None)

    assert set(sessions) == {"a"}
    session = sessions["a"]
    assert session.input_tokens == 5
    assert session.output_tokens == 150
    assert session.cache_read_tokens == 3000
    assert session.cache_creation_tokens == 500
    assert session.models == {"claude-opus-5"}


def test_since_filters_out_earlier_messages(tmp_path: Path) -> None:
    transcript = tmp_path / "session-b.jsonl"
    _write_transcript(
        transcript,
        [
            _assistant_record("b", "2026-09-14T08:00:00Z", "claude-sonnet-5", output_tokens=10),
            _assistant_record("b", "2026-09-15T08:00:00Z", "claude-sonnet-5", output_tokens=20),
        ],
    )

    since = datetime(2026, 9, 15, tzinfo=UTC)
    sessions = tokens.collect_usage(tokens.find_transcripts([str(tmp_path)]), since=since)

    assert sessions["b"].output_tokens == 20


def test_format_table_includes_a_total_row_summing_every_session(tmp_path: Path) -> None:
    transcript = tmp_path / "session-c.jsonl"
    _write_transcript(
        transcript,
        [
            _assistant_record("c1", "2026-09-14T08:00:00Z", "claude-opus-5", output_tokens=10),
            _assistant_record("c2", "2026-09-14T08:00:00Z", "claude-sonnet-5", output_tokens=20),
        ],
    )

    sessions = tokens.collect_usage(tokens.find_transcripts([str(tmp_path)]), since=None)
    table = tokens.format_table(sessions)

    assert "c1" in table
    assert "c2" in table
    total_line = next(line for line in table.splitlines() if line.startswith("TOTAL"))
    fields = total_line.split()
    assert fields[0] == "TOTAL"
    assert fields[-4:] == ["30", "0", "0", "0"]
