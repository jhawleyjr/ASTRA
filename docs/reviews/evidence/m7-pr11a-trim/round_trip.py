"""M7 PR 11a's trim evidence (docs/plans/m7-pr11a-control-peer.md, commit 4; Joe's amendment 4).

The four 13.x configs carry case 11's trim. astra trim refuses a transport block by design (ADR-0029
Context; the M7 plan's Q9 (b)), so the trim is solved on case 11's untrimmed base, and the 13.x
configs carry its text. This script shows, on this branch:
1. case 11's base round trip, rerun by PR 9's route and with its command
   (docs/reviews/evidence/m7-pr9-trim/round_trip.py, base11.yaml.in): astra.trim re-solves case 11's
   base, and the committed case 11 config rebuilds that result bit for bit (state, controls,
   altitude, Simulation.derivative's residual), and re-trimming it is idempotent;
2. the pre-registered bit identity of each 13.x config's state and control text with case 11's:
   the vehicle.initial_controls and initial.state lines are case 11's, character for character;
3. each 13.x config, built with its transport block, starts at that same trim result bit for bit;
4. D3: each 13.x config's transport.peer.trim.{elevator, throttle} equals its
   vehicle.initial_controls.controls.{elevator, throttle} bit for bit (one trim result), and
   (the M7 plan's Q11 as amended 2026-10-13) its transport.peer.trim.theta equals its
   initial.state.euler_nb[1] and the trim's solved alpha_rad (theta = alpha at gamma = 0) bit
   for bit.

Run from the repository root, after `just python`:
    .venv/bin/python -I -B docs/reviews/evidence/m7-pr11a-trim/round_trip.py
It prints its log to stdout and exits 1 on any mismatch.
"""

from __future__ import annotations

import shutil
import struct
import sys
import tempfile
from pathlib import Path

import numpy as np
import yaml

import astra.trim
from astra import _core

ROOT = Path(__file__).resolve().parents[4]
BASE = ROOT / "docs/reviews/evidence/m7-pr9-trim/base11.yaml.in"
CASE11 = ROOT / "aircraft/f16/configs/case11_trim_check_subsonic.yaml"
# Case 11's setpoint, as its config's header comment states it (PR 9's CASE11).
SPEED, ALTITUDE, HEADING = 172.42091752452774, 3051.9624000000003, 0.7853981633974483
CONFIGS = [
    "aircraft/f16/configs/case13p1_subsonic_altitude_change.yaml",
    "aircraft/f16/configs/case13p2_subsonic_airspeed_change.yaml",
    "aircraft/f16/configs/case13p3_subsonic_heading_change.yaml",
    "aircraft/f16/configs/case13p4_subsonic_lateral_side_step.yaml",
]


def bits(x: float) -> int:
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def trim_block(text: str) -> list[str]:
    """The vehicle.initial_controls and initial.state lines, as written."""
    lines = text.splitlines()
    out: list[str] = []
    take = False
    for line in lines:
        if line.startswith("  initial_controls:") or line.startswith("initial:"):
            take = True
        elif line and not line.startswith(" "):
            take = False
        elif line.startswith("  ") and not line.startswith("    ") and line.strip() != "":
            take = line.startswith("  initial_controls:") or line.startswith("  state:")
        if take:
            out.append(line)
    return out


def residual_of(sim: _core.Simulation, state: np.ndarray) -> np.ndarray:
    derivative = np.array(sim.derivative(state))
    return np.concatenate(
        [
            derivative[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE],
            derivative[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE],
        ]
    )


def rebuilds(config_path: Path, result: astra.trim.TrimResult) -> dict[str, bool]:
    sim = _core.Simulation.from_config(
        _core.Config.from_file(config_path), _core.default_plugin_dir()
    )
    state = np.array(sim.vehicles[0].state[: _core.STATE_SIZE])
    config = _core.Config.from_file(config_path)
    return {
        "state": bool(np.array_equal(state, result.state)),
        "controls": all(
            config.number(f"vehicle.initial_controls.{name}") == result.controls[name]
            for name in sim.input_names()
        ),
        "altitude": bool(state[_core.POSITION + 2] == ALTITUDE),
        "residual": bool(np.array_equal(residual_of(sim, state), result.residual)),
    }


def main() -> int:
    failures = 0
    print("M7 PR 11a: the 13.x configs' trim, by PR 9's route on this branch")
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp) / "base11.yaml"
        shutil.copyfile(BASE, base)
        result = astra.trim.trim_straight_and_level(
            base, SPEED, ALTITUDE, heading_rad=HEADING, rate_convention="n_frame"
        )
    print(f"1. case 11's base round trip ({CASE11.relative_to(ROOT)}):")
    checks = rebuilds(CASE11, result)
    again = astra.trim.trim_straight_and_level(
        CASE11, SPEED, ALTITUDE, heading_rad=HEADING, rate_convention="n_frame"
    )
    checks["idempotent"] = bool(
        np.array_equal(again.state, result.state)
        and again.controls == result.controls
        and np.array_equal(again.residual, result.residual)
    )
    for name, ok in checks.items():
        print(f"  {name:10s} {'bit for bit' if ok else 'MISMATCH'}")
        failures += 0 if ok else 1
    print(f"  theta      {result.alpha_rad!r} rad")
    for name, value in result.controls.items():
        print(f"  {name:22s} {value!r}")

    reference = trim_block(CASE11.read_text(encoding="utf-8"))
    for rel in CONFIGS:
        path = ROOT / rel
        print(f"{rel}:")
        text_ok = trim_block(path.read_text(encoding="utf-8")) == reference
        print(
            f"  2. trim text {'identical to case 11' if text_ok else 'DIFFERS from case 11'}"
            f" ({len(reference)} lines)"
        )
        failures += 0 if text_ok else 1
        for name, ok in rebuilds(path, result).items():
            print(f"  3. {name:10s} {'bit for bit' if ok else 'MISMATCH'}")
            failures += 0 if ok else 1
        config = _core.Config.from_file(path)
        for key in ("elevator", "throttle"):
            peer = config.number(f"transport.peer.trim.{key}")
            vehicle = config.number(f"vehicle.initial_controls.controls.{key}")
            ok = bits(peer) == bits(vehicle)
            print(
                f"  4. transport.peer.trim.{key} {peer!r} "
                f"{'equals' if ok else 'DIFFERS from'} the initial control {vehicle!r}"
            )
            failures += 0 if ok else 1
        theta = config.number("transport.peer.trim.theta")
        # Config's Python binding has no sequence index, so the pitch is read with PyYAML, from the
        # same text.
        pitch = float(
            yaml.safe_load(path.read_text(encoding="utf-8"))["initial"]["state"]["euler_nb"][1]
        )
        ok = bits(theta) == bits(pitch) == bits(result.alpha_rad)
        print(
            f"  4. transport.peer.trim.theta {theta!r} "
            f"{'equals' if ok else 'DIFFERS from'} initial.state.euler_nb[1] {pitch!r}"
            f" and the trim's solved alpha_rad (= theta, level) {result.alpha_rad!r}"
        )
        failures += 0 if ok else 1
    print(f"round trip: {'OK' if failures == 0 else f'{failures} mismatch(es)'}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
