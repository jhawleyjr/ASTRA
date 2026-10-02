#!/usr/bin/env python3
"""R19's smoke for the frame-time benchmark (M6 PR 9, docs/plans/m6-pr9-benchmark.md).

Runs astra_frame_benchmark at 1000 frames into a fresh directory and checks every JSON field (D7),
its type, and the frame counts. It never asserts a speed: the only numeric checks follow from the
definitions (median <= p99 <= max, mean <= max, the margins against dt), which a slow machine passes
as well as a fast one. Then the refusals (D6), each writing no file: a config that halts in its
warm-up, and one that halts inside the timed loop after 1500 good frames, each exit 1 naming the
check and the frame; a bad argument exits 64. The benchmark makes its log directory under TMPDIR,
which each run points at its own scratch directory, so a leftover file is seen and no other run's
file is.

Usage: check_frame_benchmark.py BENCHMARK SOURCE_DIR. Exit 0 with "frame-benchmark smoke: OK", or 1
with the first failure. Stdlib only.
"""

from __future__ import annotations

import json
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

FRAMES = 1000
REPEATS = 3
STEP_CONFIGS = [
    "aircraft/notional_transport/configs/cruise.yaml",
    "aircraft/notional_transport/configs/cruise_still_air.yaml",
    "tests/configs/dummy_1000.yaml",
]
END_TO_END = {  # case: (pattern, frames)
    "cruise_1": ("run_close", 1),
    "cruise_n": ("run_close", FRAMES),
    "cruise_slow_rate_1": ("run_close", 1),
    "cruise_slow_rate_n": ("run_close", FRAMES),
    "cruise_n_step_flush": ("step_flush_close", FRAMES),
}
STEP_NUMBERS = [
    "dt",
    "mean",
    "median",
    "p99",
    "max",
    "first_frame",
    "frames_per_second",
    "margin_p99",
    "margin_max",
]


class SmokeError(Exception):
    pass


def check(condition: bool, message: str) -> None:
    if not condition:
        raise SmokeError(message)


def is_number(value: Any) -> bool:
    return isinstance(value, int | float) and not isinstance(value, bool) and math.isfinite(value)


def is_count(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def nonempty_string(value: Any) -> bool:
    return isinstance(value, str) and value != ""


def check_keys(where: str, obj: Any, keys: set[str]) -> None:
    check(isinstance(obj, dict), f"{where} is not an object")
    check(set(obj) == keys, f"{where} keys are {sorted(obj)}, expected {sorted(keys)}")


def close(a: float, b: float) -> bool:
    return math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-15)


def check_header(report: dict[str, Any]) -> None:
    check_keys(
        "report",
        report,
        {"schema", "units", "build", "host", "clock", "frames_source", "step", "end_to_end"},
    )
    check(report["schema"] == "astra.frame_benchmark/1", f"schema is {report['schema']!r}")
    check(report["units"] == {"time": "s", "rate": "1/s", "margin": "fraction of dt"}, "units")
    build = report["build"]
    check_keys("build", build, {"type", "built_with", "compiler", "git_describe", "core_version"})
    for key in ("built_with", "compiler", "git_describe", "core_version"):
        check(nonempty_string(build[key]), f"build.{key} is empty")
    check(isinstance(build["type"], str), "build.type is not a string")
    host = report["host"]
    check_keys(
        "host",
        host,
        {"cpu_model", "logical_cpus", "governor", "kernel", "loadavg_start", "loadavg_end"},
    )
    for key in ("cpu_model", "governor", "kernel"):
        check(nonempty_string(host[key]), f"host.{key} is empty")
    check(is_count(host["logical_cpus"]) and host["logical_cpus"] > 0, "host.logical_cpus")
    for key in ("loadavg_start", "loadavg_end"):
        load = host[key]
        check(isinstance(load, list) and len(load) == 3, f"host.{key} is not three numbers")
        check(all(is_number(v) and v >= 0 for v in load), f"host.{key} holds a bad value")
    clock = report["clock"]
    check_keys("clock", clock, {"name", "period", "overhead"})
    check(clock["name"] == "std::chrono::steady_clock", "clock.name")
    check(is_number(clock["period"]) and clock["period"] > 0, "clock.period")
    check(is_number(clock["overhead"]) and clock["overhead"] >= 0, "clock.overhead")
    check(report["frames_source"] == "override", "frames_source is not override under --frames")


def check_step(entry: dict[str, Any], config: str) -> None:
    where = f"step[{config}]"
    check_keys(where, entry, {"config", "frames", "warmup_frames", "samples", *STEP_NUMBERS})
    check(entry["config"] == config, f"{where}.config is {entry['config']!r}")
    check(entry["frames"] == FRAMES, f"{where}.frames is {entry['frames']}, not {FRAMES}")
    check(entry["samples"] == FRAMES, f"{where}.samples is {entry['samples']}: one per frame")
    check(entry["warmup_frames"] == min(FRAMES, 1000), f"{where}.warmup_frames")
    for key in STEP_NUMBERS:
        check(is_number(entry[key]), f"{where}.{key} is not a finite number")
    check(entry["dt"] == 0.001, f"{where}.dt is {entry['dt']}")
    check(entry["median"] <= entry["p99"] <= entry["max"], f"{where}: median <= p99 <= max fails")
    check(entry["mean"] <= entry["max"], f"{where}: mean > max")
    check(entry["first_frame"] <= entry["max"], f"{where}: first_frame > max")
    check(entry["frames_per_second"] > 0, f"{where}.frames_per_second")
    # fps = n / sum and mean = sum / n, so fps * mean = 1.
    check(close(entry["frames_per_second"] * entry["mean"], 1.0), f"{where}: fps != 1 / mean")
    check(close(entry["margin_p99"], 1 - entry["p99"] / entry["dt"]), f"{where}.margin_p99")
    check(close(entry["margin_max"], 1 - entry["max"] / entry["dt"]), f"{where}.margin_max")


def check_end_to_end(entry: dict[str, Any]) -> None:
    check_keys(
        "end_to_end entry",
        entry,
        {
            "case",
            "config",
            "pattern",
            "frames",
            "repeats",
            "seconds",
            "median",
            "frames_per_second",
        },
    )
    where = f"end_to_end[{entry['case']}]"
    pattern, frames = END_TO_END[entry["case"]]
    check(entry["pattern"] == pattern, f"{where}.pattern is {entry['pattern']!r}")
    check(entry["frames"] == frames, f"{where}.frames is {entry['frames']}, not {frames}")
    check(nonempty_string(entry["config"]), f"{where}.config")
    check(entry["repeats"] == REPEATS, f"{where}.repeats is {entry['repeats']}")
    seconds = entry["seconds"]
    check(isinstance(seconds, list) and len(seconds) == REPEATS, f"{where}.seconds")
    check(all(is_number(s) and s > 0 for s in seconds), f"{where}.seconds holds a bad value")
    check(entry["median"] == sorted(seconds)[REPEATS // 2], f"{where}.median is not the median")
    check(close(entry["frames_per_second"], frames / entry["median"]), f"{where}.frames_per_second")


def run(benchmark: str, tmp: Path, *args: str) -> subprocess.CompletedProcess[str]:
    tmp.mkdir()
    env = {**os.environ, "TMPDIR": str(tmp)}
    result = subprocess.run(
        [benchmark, *args], capture_output=True, text=True, check=False, timeout=600, env=env
    )
    leftovers = sorted(str(p.relative_to(tmp)) for p in tmp.rglob("*"))
    if result.returncode == 0:
        check(leftovers == [], f"files left behind under TMPDIR: {leftovers}")
    return result


def check_report(benchmark: str, scratch: Path) -> None:
    out = scratch / "report.json"
    result = run(benchmark, scratch / "tmp_report", "--frames", str(FRAMES), "--out", str(out))
    check(result.returncode == 0, f"benchmark exited {result.returncode}:\n{result.stderr}")
    report = json.loads(out.read_text(encoding="utf-8"))
    check_header(report)
    steps = report["step"]
    check(isinstance(steps, list) and len(steps) == len(STEP_CONFIGS), "step has the wrong length")
    for entry, config in zip(steps, STEP_CONFIGS, strict=True):
        check_step(entry, config)
    cases = report["end_to_end"]
    check(isinstance(cases, list), "end_to_end is not a list")
    check([c.get("case") for c in cases] == list(END_TO_END), "end_to_end cases differ")
    for entry in cases:
        check_end_to_end(entry)


def check_halt(benchmark: str, scratch: Path, name: str, config: Path, frame: int) -> str:
    out = scratch / f"{name}.json"
    result = run(benchmark, scratch / f"tmp_{name}", "--config", str(config), "--out", str(out))
    check(result.returncode == 1, f"{name}: a halting config exited {result.returncode}, not 1")
    check(
        f"halted at frame {frame}," in result.stderr, f"{name}: no halt frame in:\n{result.stderr}"
    )
    check("check NON_FINITE_WRENCH" in result.stderr, f"{name}: no halt check in:\n{result.stderr}")
    check("nothing was recorded" in result.stderr, f"{name}: no refusal in:\n{result.stderr}")
    check(not out.exists(), f"{name}: a halting config still wrote its JSON")
    return result.stderr


def check_refusals(benchmark: str, source_dir: Path, scratch: Path) -> None:
    # tests/configs/health_nan_wrench.yaml: 1000 frames, NaN wrench at frame 500. Its 1000-frame
    # warm-up halts first, before a single frame is timed.
    halting = source_dir / "tests/configs/health_nan_wrench.yaml"
    stderr = check_halt(benchmark, scratch, "warmup_halt", halting, 500)
    check("(warm-up)" in stderr, f"warmup_halt: not the warm-up's halt:\n{stderr}")
    # The same config at 2000 frames with the NaN at frame 1500: the warm-up's 1000 frames pass, and
    # the timed loop halts after 1500 good samples, which must not be recorded either (C-19).
    late = scratch / "nan_late.yaml"
    text = halting.read_text(encoding="utf-8")
    check(text.count("frames: 1000") == 1 and text.count("nan_frame: 500") == 1, "fixture changed")
    late.write_text(
        text.replace("frames: 1000", "frames: 2000").replace("nan_frame: 500", "nan_frame: 1500"),
        encoding="utf-8",
    )
    stderr = check_halt(benchmark, scratch, "timed_halt", late, 1500)
    check("(warm-up)" not in stderr, f"timed_halt: halted in the warm-up instead:\n{stderr}")

    out = scratch / "usage.json"
    result = run(benchmark, scratch / "tmp_usage_frames", "--frames", "0", "--out", str(out))
    check(result.returncode == 64, f"--frames 0 exited {result.returncode}, not 64")
    result = run(benchmark, scratch / "tmp_usage_flag", "--bogus")
    check(result.returncode == 64, f"an unknown argument exited {result.returncode}, not 64")
    result = run(benchmark, scratch / "tmp_usage_out", "--frames", "10")
    check(result.returncode == 64, f"a missing --out exited {result.returncode}, not 64")
    check(not out.exists(), "a usage error still wrote its JSON")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_frame_benchmark.py BENCHMARK SOURCE_DIR", file=sys.stderr)
        return 2
    benchmark, source_dir = sys.argv[1], Path(sys.argv[2])
    try:
        with tempfile.TemporaryDirectory(prefix="astra_frame_smoke_") as scratch:
            check_report(benchmark, Path(scratch))
            check_refusals(benchmark, source_dir, Path(scratch))
    except SmokeError as error:
        print(f"frame-benchmark smoke: {error}", file=sys.stderr)
        return 1
    print("frame-benchmark smoke: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
