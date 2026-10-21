"""M7b PR 4's R15: the two hooks leave shipped behavior bit-identical
(docs/plans/m7b-pr4-f16-symmetry.md, section 11; plan decision 5).

One process on this build's astra._core flies each shipped config twice: once with the plugins as
they were before the hooks, and once with this build's. "One build" is read as PR 3's O11 reads
git_describe: one libastra_core build, equal in both passes' logs.
- The "before" plugin directory is a copy of this build's, with the two plugins the hooks touch
  replaced by copies saved from build/python before the hooks were added (--before-plugins). Those
  are aircraft/f16/physical/aero and aircraft/f16/sim/transport/nesc_control.
- --before-sha names the commit build/python was built at for that copy. The log records that
  `git diff <before-sha> HEAD -- core icd` is empty, so the swap compares only the two hooked
  plugins, and it lists every shipped file the range changes (amendment 8).

The configs, all tracked and all unchanged:
- aircraft/f16/configs/case13p3_subsonic_heading_change.yaml: the exchange recorder's bytes;
- aircraft/f16/configs/case13p4_subsonic_lateral_side_step.yaml: the same;
- aircraft/f16/configs/case11_trim_check_subsonic.yaml, case 11's open-loop run. It has no transport
  and so no exchange recorder, so this script records, at every frame, the 13 rigid-body doubles and
  the four latched controls in the recorder's state layout (a uint64 frame, then the doubles).
Each run also writes a log. Every HDF5 object and attribute must be byte-equal between the passes,
except /plugins' sha256 for the two swapped plugins, whose values are printed. git rev-parse HEAD
and git status --porcelain are read before the first run and after the last, and must be equal.

Run from the repository root, after `just python` on the hooked tree:
    .venv/bin/python -I -B docs/reviews/evidence/m7b-pr4-r15/r15_before_after.py \
        --before-plugins <dir> --before-sha <sha>
It prints its log to stdout and exits 1 on any difference.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

import h5py  # pyright: ignore[reportMissingTypeStubs]
import numpy as np

from astra import _core, _yaml_route

ROOT = Path(__file__).resolve().parents[4]
HOOKED = (
    "libastra_aircraft_f16_physical_aero.so",
    "libastra_aircraft_f16_sim_transport_nesc_control.so",
)
HOOKED_PATHS = ("aircraft/f16/physical/aero", "aircraft/f16/sim/transport/nesc_control")
CONFIGS = (
    ("13.3", "aircraft/f16/configs/case13p3_subsonic_heading_change.yaml", "recorder"),
    ("13.4", "aircraft/f16/configs/case13p4_subsonic_lateral_side_step.yaml", "recorder"),
    ("case 11", "aircraft/f16/configs/case11_trim_check_subsonic.yaml", "states"),
)
CONTROLS = (
    "v0.controls.elevator",
    "v0.controls.aileron",
    "v0.controls.rudder",
    "v0.controls.throttle",
)


def git(*args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=ROOT, capture_output=True, text=True, check=True
    ).stdout.strip()


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fly(config_path: str, plugin_dir: Path, log: Path, kind: str) -> bytes:
    text = (ROOT / config_path).read_text(encoding="utf-8")
    data: Any = _yaml_route.load(text, config_path, error=_core.ConfigError)
    data["log"] = {"path": str(log)}
    sim = _core.Simulation.from_config(_core.Config.from_dict(data, origin=config_path), plugin_dir)
    if kind == "recorder":
        halt = sim.run()
        out = sim.exchange_recorder_bytes()
        assert out is not None, f"{config_path}: no exchange recorder"
    else:
        chunks: list[bytes] = []

        def record() -> None:
            state = np.asarray(sim.vehicles[0].state[: _core.STATE_SIZE], dtype=np.float64)
            controls = np.asarray([sim.registry.find(name) for name in CONTROLS], dtype=np.float64)
            chunks.append(struct.pack("<Q", sim.clock.frame) + state.tobytes() + controls.tobytes())

        record()
        halt = None
        for _ in range(sim.frames):
            halt = sim.run(1)  # run(), unlike step(), drains the log's ring
            if halt:
                break
            record()
        out = b"".join(chunks)
    assert not halt, f"{config_path}: halted: {halt}"
    sim.flush_log()
    sim.close_log()
    return out


def as_bytes(value: Any) -> bytes:
    """A value's exact content: its dtype, shape and bytes for numbers, its text for strings (an
    object array's own bytes are pointers, which differ between reads)."""
    array = np.asarray(value)
    if array.dtype.kind in "OSU":
        items = [x.decode() if isinstance(x, bytes) else str(x) for x in array.ravel()]
        return repr((array.shape, items)).encode()
    return repr((array.dtype.str, array.shape)).encode() + array.tobytes()


def objects(path: Path) -> dict[str, bytes]:
    """Every attribute and dataset of an HDF5 file, as exact content, keyed by its path."""
    found: dict[str, bytes] = {}
    with h5py.File(path, "r") as f:

        def visit(name: str, obj: Any) -> None:
            for key, value in obj.attrs.items():
                found[f"{name}@{key}"] = as_bytes(value)
            if isinstance(obj, h5py.Dataset) and name != "plugins":
                found[name] = as_bytes(obj[()])

        for key, value in f.attrs.items():
            found[f"/@{key}"] = as_bytes(value)
        f.visititems(visit)
    return found


def root_attr(path: Path, key: str) -> str:
    with h5py.File(path, "r") as f:
        value = f.attrs[key]
        return value.decode() if isinstance(value, bytes) else str(value)


def plugin_rows(path: Path) -> tuple[list[tuple[Any, ...]], dict[str, str]]:
    """/plugins' rows without the two hooked plugins' sha256, and those sha256 values."""
    with h5py.File(path, "r") as f:
        table: Any = f["plugins"][()]  # pyright: ignore[reportIndexIssue]
        names: tuple[str, ...] = table.dtype.names
        rows: list[tuple[Any, ...]] = []
        hashes: dict[str, str] = {}
        for row in table:
            values = {n: (row[n].decode() if isinstance(row[n], bytes) else row[n]) for n in names}
            if values["path"] in HOOKED_PATHS:
                hashes[values["path"]] = str(values["sha256"])
                values["sha256"] = "<the swapped plugin>"
            rows.append(tuple(str(values[n]) for n in names))
        return rows, hashes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--before-plugins", type=Path, required=True)
    parser.add_argument("--before-sha", required=True)
    args = parser.parse_args()
    failures: list[str] = []

    head, status = git("rev-parse", "HEAD"), git("status", "--porcelain")
    print("M7b PR 4: R15, the hooks' before-and-after")
    print("  (docs/plans/m7b-pr4-f16-symmetry.md, section 11)")
    print(f"  HEAD {head}; worktree {'NOT clean' if status else 'clean'}")
    core_diff = git("diff", "--name-only", args.before_sha, "HEAD", "--", "core", "icd")
    shipped = git("diff", "--name-only", args.before_sha, "HEAD", "--", "aircraft", "plugins")
    print(f"  git diff {args.before_sha}..HEAD -- core icd: {core_diff or 'empty'}")
    print(f"  git diff {args.before_sha}..HEAD -- aircraft plugins:")
    for line in shipped.splitlines():
        print(f"    {line}")
    if core_diff:
        failures.append("core or icd changed since the before-build")

    after = Path(_core.default_plugin_dir())
    with tempfile.TemporaryDirectory() as tmp:
        before = Path(tmp) / "plugins"
        shutil.copytree(after, before)
        for name in HOOKED:
            shutil.copyfile(args.before_plugins / name, before / name)
        for name in HOOKED:
            print(f"  {name}: before {sha256(before / name)}")
            print(f"  {' ' * len(name)}  after  {sha256(after / name)}")
        for label, config, kind in CONFIGS:
            logs = {side: Path(tmp) / f"{side}-{label.replace(' ', '')}.h5" for side in ("b", "a")}
            out_before = fly(config, before, logs["b"], kind)
            out_after = fly(config, after, logs["a"], kind)
            same = out_before == out_after
            digest = hashlib.sha256(out_after).hexdigest()
            print(
                f"  {label} ({config}): {kind} {len(out_after)} bytes,"
                f" {'byte-equal' if same else 'DIFFERS'}, sha256 {digest}"
            )
            if not same:
                failures.append(f"{label}'s {kind} differs")
            ob, oa = objects(logs["b"]), objects(logs["a"])
            differing = sorted(k for k in set(ob) | set(oa) if ob.get(k) != oa.get(k))
            rows_b, hashes_b = plugin_rows(logs["b"])
            rows_a, hashes_a = plugin_rows(logs["a"])
            describes = [root_attr(logs[side], "git_describe") for side in ("b", "a")]
            print(
                f"    log: {len(oa)} objects, {len(differing)} differing {differing};"
                f" git_describe {describes[0]!r} and {describes[1]!r}"
            )
            if describes[0] != describes[1]:
                failures.append(f"{label}'s git_describe differs: not one libastra_core build")
            for path in HOOKED_PATHS:
                print(f"    /plugins {path}: sha256 before {hashes_b.get(path)}")
                print(f"    {' ' * len(path)}           after  {hashes_a.get(path)}")
            if differing:
                failures.append(f"{label}'s log differs in {differing[:5]}")
            if rows_b != rows_a:
                failures.append(f"{label}'s /plugins differs outside the swapped sha256")

    head2, status2 = git("rev-parse", "HEAD"), git("status", "--porcelain")
    same_state = head2 == head and status2 == status
    print(f"  HEAD and status after the last run equal those before the first: {same_state}")
    if not same_state:
        failures.append("the worktree state changed between the runs")
    if failures:
        print("FAIL: " + "; ".join(failures))
        return 1
    print("OK: every recorder, state stream and log object is byte-equal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
