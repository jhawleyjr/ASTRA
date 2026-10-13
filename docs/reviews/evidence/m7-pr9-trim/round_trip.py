"""M7 PR 9's trim evidence (docs/plans/m7-pr9-open-loop.md, D2): each committed case 11 and 12
config, and each SIM 2 control config, rebuilds its trim bit for bit by PR 5's round-trip criterion.

For each config: astra.trim re-solves the trim from the untrimmed base (base11.yaml.in,
base12.yaml.in, beside this file, as the configs were emitted) at the config's explicit setpoint.
The committed config then rebuilds that result exactly:
- the Simulation built from it starts at TrimResult.state;
- every input's committed value equals TrimResult.controls;
- the altitude read back equals the setpoint;
- Simulation.derivative at that state equals TrimResult.residual.
It also trims the committed config itself with the same flags (idempotence, reported).

Run from the repository root, after `just python`:
    .venv/bin/python -I -B docs/reviews/evidence/m7-pr9-trim/round_trip.py
It prints its log to stdout and exits 1 on any mismatch. The bases are named .yaml.in so that they
are not tracked configs (tests/python/test_config_keys.py).
"""

from __future__ import annotations

import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np

import astra.trim
from astra import _core

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent

# (committed config, base, true airspeed m/s, altitude m, heading rad, rate convention), the
# setpoints as each config's header comment states them.
CASE11 = (172.42091752452774, 3051.9624000000003, 0.7853981633974483)
CASE12 = (609.5999998391766, 9147.9624, 0.7853981633974483)
RUNS = [
    ("aircraft/f16/configs/case11_trim_check_subsonic.yaml", "base11.yaml.in", *CASE11, "n_frame"),
    (
        "aircraft/f16/configs/case12_trim_check_supersonic.yaml",
        "base12.yaml.in",
        *CASE12,
        "n_frame",
    ),
    (
        "tests/physics/nesc/case11_f16_zero_inertial_trim.yaml",
        "base11.yaml.in",
        *CASE11,
        "zero_inertial",
    ),
    (
        "tests/physics/nesc/case12_f16_zero_inertial_trim.yaml",
        "base12.yaml.in",
        *CASE12,
        "zero_inertial",
    ),
]


def trim(
    path: Path, speed: float, alt: float, psi: float, convention: str
) -> astra.trim.TrimResult:
    return astra.trim.trim_straight_and_level(
        path,
        speed,
        alt,
        heading_rad=psi,
        rate_convention=convention,  # type: ignore[arg-type]
    )


def residual_of(sim: _core.Simulation, state: np.ndarray) -> np.ndarray:
    derivative = np.array(sim.derivative(state))
    return np.concatenate(
        [
            derivative[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE],
            derivative[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE],
        ]
    )


def main() -> int:
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for config_rel, base_name, speed, alt, psi, convention in RUNS:
            config_path = ROOT / config_rel
            # astra.trim reads a .yaml path; the base is copied under that suffix.
            base_path = Path(tmp) / (base_name.removesuffix(".in"))
            shutil.copyfile(HERE / base_name, base_path)
            result = trim(base_path, speed, alt, psi, convention)

            sim = _core.Simulation.from_config(
                _core.Config.from_file(config_path), _core.default_plugin_dir()
            )
            state = np.array(sim.vehicles[0].state[: _core.STATE_SIZE])
            config = _core.Config.from_file(config_path)
            checks = {
                "state": bool(np.array_equal(state, result.state)),
                "controls": all(
                    config.number(f"vehicle.initial_controls.{name}") == result.controls[name]
                    for name in sim.input_names()
                ),
                "altitude": bool(state[_core.POSITION + 2] == alt),
                "residual": bool(np.array_equal(residual_of(sim, state), result.residual)),
            }
            again = trim(config_path, speed, alt, psi, convention)
            checks["idempotent"] = bool(
                np.array_equal(again.state, result.state)
                and again.controls == result.controls
                and np.array_equal(again.residual, result.residual)
            )
            print(f"{config_rel} ({convention}):")
            for name, ok in checks.items():
                print(f"  {name:10s} {'bit for bit' if ok else 'MISMATCH'}")
                failures += 0 if ok else 1
            print(f"  theta      {result.alpha_rad!r} rad")
            for name in sim.input_names():
                print(f"  {name:22s} {result.controls[name]!r}")
            print("  residual   " + ", ".join(repr(float(v)) for v in result.residual))
    print(f"round trip: {'OK' if failures == 0 else f'{failures} mismatch(es)'}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
